// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "glslcompletionassist.h"

#include <glsl/glslengine.h>
#include <glsl/glsllexer.h>
#include <glsl/glslparser.h>
#include <glsl/glslsemantic.h>
#include <glsl/glslsymbols.h>
#include <glsl/glslastdump.h>

#include <coreplugin/idocument.h>

#include <texteditor/completionsettings.h>
#include <texteditor/codeassist/assistproposalitem.h>
#include <texteditor/codeassist/completionassistprovider.h>
#include <texteditor/codeassist/functionhintproposal.h>
#include <texteditor/codeassist/genericproposal.h>
#include <texteditor/codeassist/genericproposalmodel.h>
#include <texteditor/texteditorsettings.h>

#include <cplusplus/ExpressionUnderCursor.h>
#include <cplusplus/Icons.h>

#include <utils/icon.h>

#include <QIcon>

using namespace TextEditor;

namespace GlslEditor {
namespace Internal {

Document::~Document()
{
    delete _globalScope;
    delete _engine;
}

GLSL::Scope *Document::scopeAt(int position) const
{
    for (const Range &c : _cursors) {
        if (position >= c.cursor.selectionStart() && position <= c.cursor.selectionEnd())
            return c.scope;
    }
    return _globalScope;
}

void Document::addRange(const QTextCursor &cursor, GLSL::Scope *scope)
{
    Range c;
    c.cursor = cursor;
    c.scope = scope;
    _cursors.append(c);
}


enum CompletionOrder {
    SpecialMemberOrder = -5
};

static bool isActivationChar(const QChar &ch)
{
    return ch == QLatin1Char('(') || ch == QLatin1Char('.') || ch == QLatin1Char(',');
}

static bool isIdentifierChar(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_');
}

static bool isDelimiter(QChar ch)
{
    switch (ch.unicode()) {
    case '{':
    case '}':
    case '[':
    case ']':
    case ')':
    case '?':
    case '!':
    case ':':
    case ';':
    case ',':
    case '+':
    case '-':
    case '*':
    case '/':
        return true;

    default:
        return false;
    }
}

static bool checkStartOfIdentifier(const QString &word)
{
    if (! word.isEmpty()) {
        const QChar ch = word.at(0);
        if (ch.isLetter() || ch == QLatin1Char('_'))
            return true;
    }

    return false;
}

enum IconTypes {
    IconTypeAttribute,
    IconTypeUniform,
    IconTypeKeyword,
    IconTypeVarying,
    IconTypeConst,
    IconTypeVariable,
    IconTypeType,
    IconTypeFunction,
    IconTypeOther
};

static QIcon glslIcon(IconTypes iconType)
{
    using namespace CPlusPlus;
    using namespace Utils;

    const char member[] = ":/codemodel/images/member.png";

    switch (iconType) {
    case IconTypeType:
        return Utils::CodeModelIcon::iconForType(Utils::CodeModelIcon::Class);
    case IconTypeConst:
        return Utils::CodeModelIcon::iconForType(Utils::CodeModelIcon::Enumerator);
    case IconTypeKeyword:
        return Utils::CodeModelIcon::iconForType(Utils::CodeModelIcon::Keyword);
    case IconTypeFunction:
        return Utils::CodeModelIcon::iconForType(Utils::CodeModelIcon::FuncPublic);
    case IconTypeVariable:
        return Utils::CodeModelIcon::iconForType(Utils::CodeModelIcon::VarPublic);
    case IconTypeAttribute: {
        static const QIcon icon =
                Icon({{member, Theme::IconsCodeModelAttributeColor}}, Icon::Tint).icon();
        return icon;
    }
    case IconTypeUniform: {
        static const QIcon icon =
                Icon({{member, Theme::IconsCodeModelUniformColor}}, Icon::Tint).icon();
        return icon;
    }
    case IconTypeVarying: {
        static const QIcon icon =
                Icon({{member, Theme::IconsCodeModelVaryingColor}}, Icon::Tint).icon();
        return icon;
    }
    case IconTypeOther:
    default:
        return Utils::CodeModelIcon::iconForType(Utils::CodeModelIcon::Namespace);
    }
}

// ----------------------------
// GlslCompletionAssistProvider
// ----------------------------
struct FunctionItem
{
    FunctionItem() = default;
    explicit FunctionItem(const GLSL::Function *function);
    QString prettyPrint(int currentArgument) const;
    QString returnValue;
    QString name;
    QStringList argsWithType;
};

FunctionItem::FunctionItem(const GLSL::Function *function)
{
    Q_ASSERT(function);
    returnValue = function->returnType()->toString();
    name = function->name();
    for (auto arg : function->arguments())
        argsWithType.append(arg->type()->toString() + QLatin1Char(' ') + arg->name());
}

QString FunctionItem::prettyPrint(int currentArgument) const
{
    QString result = returnValue + QLatin1Char(' ') + name + QLatin1Char('(');
    for (int i = 0; i < argsWithType.size(); ++i) {
        if (i != 0)
            result += QLatin1String(", ");
        if (currentArgument == i)
            result += QLatin1String("<b>");
        result += argsWithType.at(i);
        if (currentArgument == i)
            result += QLatin1String("</b>");
    }
    result += QLatin1Char(')');
    return result;
}

// -----------------------------
// GlslFunctionHintProposalModel
// -----------------------------
class GlslFunctionHintProposalModel : public IFunctionHintProposalModel
{
public:
    GlslFunctionHintProposalModel(QVector<GLSL::Function *> functionSymbols);

    void reset() override {}
    int size() const override { return m_items.size(); }
    QString text(int index) const override;
    int activeArgument(const QString &prefix) const override;

private:
    QVector<FunctionItem> m_items;
    mutable int m_currentArg;
};

GlslFunctionHintProposalModel::GlslFunctionHintProposalModel(QVector<GLSL::Function *> symbols)
    : m_currentArg(-1)
{
    for (const GLSL::Function *symbol : symbols)
        m_items.append(FunctionItem(symbol));
}

QString GlslFunctionHintProposalModel::text(int index) const
{
    return m_items.at(index).prettyPrint(m_currentArg);
}

int GlslFunctionHintProposalModel::activeArgument(const QString &prefix) const
{
    const QByteArray &str = prefix.toLatin1();
    int argnr = 0;
    int parcount = 0;
    GLSL::Lexer lexer(nullptr, str.constData(), str.length());
    GLSL::Token tk;
    QList<GLSL::Token> tokens;
    do {
        lexer.yylex(&tk);
        tokens.append(tk);
    } while (tk.isNot(GLSL::Parser::EOF_SYMBOL));
    for (int i = 0; i < tokens.count(); ++i) {
        const GLSL::Token &tk = tokens.at(i);
        if (tk.is(GLSL::Parser::T_LEFT_PAREN))
            ++parcount;
        else if (tk.is(GLSL::Parser::T_RIGHT_PAREN))
            --parcount;
        else if (! parcount && tk.is(GLSL::Parser::T_COMMA))
            ++argnr;
    }

    if (parcount < 0)
        return -1;

    if (argnr != m_currentArg)
        m_currentArg = argnr;

    return argnr;
}

// -----------------------------
// GLSLCompletionAssistProcessor
// -----------------------------

class GlslCompletionAssistProcessor final : public TextEditor::AsyncProcessor
{
public:
    TextEditor::IAssistProposal *performAsync() final;

private:
    TextEditor::IAssistProposal *createHintProposal(const QVector<GLSL::Function *> &symbols);
    bool acceptsIdleEditor() const;

    int m_startPosition = 0;
};

static AssistProposalItem *createCompletionItem(const QString &text, const QIcon &icon, int order = 0)
{
    auto item = new AssistProposalItem;
    item->setText(text);
    item->setIcon(icon);
    item->setOrder(order);
    return item;
}

IAssistProposal *GlslCompletionAssistProcessor::performAsync()
{
    auto interface = static_cast<const GlslCompletionAssistInterface *>(this->interface());

    if (interface->reason() == IdleEditor && !acceptsIdleEditor())
        return nullptr;

    int pos = interface->position() - 1;
    QChar ch = interface->characterAt(pos);
    while (ch.isLetterOrNumber() || ch == QLatin1Char('_'))
        ch = interface->characterAt(--pos);

    CPlusPlus::ExpressionUnderCursor expressionUnderCursor(
                CPlusPlus::LanguageFeatures::defaultFeatures());
    //GLSLTextEditorWidget *edit = qobject_cast<GLSLTextEditorWidget *>(editor->widget());

    QList<GLSL::Symbol *> members;
    QStringList specialMembers;
    QList<AssistProposalItemInterface *> m_completions;

    bool functionCall = (ch == QLatin1Char('(') && pos == interface->position() - 1);

    if (ch == QLatin1Char(',')) {
        QTextCursor tc(interface->textDocument());
        tc.setPosition(pos);
        const int start = expressionUnderCursor.startOfFunctionCall(tc);
        if (start == -1)
            return nullptr;

        if (interface->characterAt(start) == QLatin1Char('(')) {
            pos = start;
            ch = QLatin1Char('(');
            functionCall = true;
        }
    }

    if (ch == QLatin1Char('.') || functionCall) {
        const bool memberCompletion = ! functionCall;
        QTextCursor tc(interface->textDocument());
        tc.setPosition(pos);

        // get the expression under cursor
        const QByteArray code = expressionUnderCursor(tc).toLatin1();
        //qDebug() << endl << "expression:" << code;

        // parse the expression
        GLSL::Engine engine;
        GLSL::Parser parser(&engine, code, code.size(), languageVariant(interface->mimeType()));
        GLSL::ExpressionAST *expr = parser.parseExpression();

#if 0
        // dump it!
        QTextStream qout(stdout, QIODevice::WriteOnly);
        GLSL::ASTDump dump(qout);
        dump(expr);
#endif

        if (Document::Ptr doc = interface->glslDocument()) {
            GLSL::Scope *currentScope = doc->scopeAt(pos);

            GLSL::Semantic sem;
            GLSL::Semantic::ExprResult exprTy = sem.expression(expr, currentScope, doc->engine());
            if (exprTy.type) {
                if (memberCompletion) {
                    if (const GLSL::VectorType *vecTy = exprTy.type->asVectorType()) {
                        members = vecTy->members();

                        // Sort the most relevant swizzle orderings to the top.
                        specialMembers += QLatin1String("xy");
                        specialMembers += QLatin1String("xyz");
                        specialMembers += QLatin1String("xyzw");
                        specialMembers += QLatin1String("rgb");
                        specialMembers += QLatin1String("rgba");
                        specialMembers += QLatin1String("st");
                        specialMembers += QLatin1String("stp");
                        specialMembers += QLatin1String("stpq");

                    } else if (const GLSL::Struct *structTy = exprTy.type->asStructType()) {
                        members = structTy->members();

                    } else if (const GLSL::InterfaceBlock *interfaceBlockTy = exprTy.type->asInterfaceBlockType()) {
                        members += interfaceBlockTy->members();

                    } else {
                        // some other type
                    }
                } else { // function completion
                    QVector<GLSL::Function *> signatures;
                    if (const GLSL::Function *funTy = exprTy.type->asFunctionType())
                        signatures.append(const_cast<GLSL::Function *>(funTy)); // ### get rid of the const_cast
                    else if (const GLSL::OverloadSet *overload = exprTy.type->asOverloadSetType())
                        signatures = overload->functions();

                    if (! signatures.isEmpty()) {
                        m_startPosition = pos + 1;
                        return createHintProposal(signatures);
                    }
                }
            } else {
                // undefined

            }

        } else {
            // sorry, there's no document
        }

    } else {
        // it's a global completion
        if (Document::Ptr doc = interface->glslDocument()) {
            GLSL::Scope *currentScope = doc->scopeAt(pos);
            bool isGlobal = !currentScope || !currentScope->scope();

            // add the members from the scope chain
            for (; currentScope; currentScope = currentScope->scope())
                members += currentScope->members();

            // add interface block fields
            if (auto globalScope = doc->globalScope()) {
                const QList<GLSL::Symbol *> globalMembers = globalScope->members();
                for (GLSL::Symbol *sym : globalMembers) {
                    if (GLSL::InterfaceBlock *iBlock = sym->asInterfaceBlock())
                        members += iBlock->members();
                }
            }

            // if this is the global scope, then add some standard Qt attribute
            // and uniform names for autocompleting variable declarations
            // this isn't a complete list, just the most common
            if (isGlobal) {
                static const char * const attributeNames[] = {
                    "qt_Vertex",
                    "qt_Normal",
                    "qt_MultiTexCoord0",
                    "qt_MultiTexCoord1",
                    "qt_MultiTexCoord2",
                    nullptr
                };
                static const char * const uniformNames[] = {
                    "qt_ModelViewProjectionMatrix",
                    "qt_ModelViewMatrix",
                    "qt_ProjectionMatrix",
                    "qt_NormalMatrix",
                    "qt_Texture0",
                    "qt_Texture1",
                    "qt_Texture2",
                    "qt_Color",
                    "qt_Opacity",
                    nullptr
                };
                for (int index = 0; attributeNames[index]; ++index)
                    m_completions << createCompletionItem(QString::fromLatin1(attributeNames[index]), glslIcon(IconTypeAttribute));
                for (int index = 0; uniformNames[index]; ++index)
                    m_completions << createCompletionItem(QString::fromLatin1(uniformNames[index]), glslIcon(IconTypeUniform));
            }
        }

 //       if (m_keywordVariant != languageVariant(interface->mimeType())) {
            int langVar = languageVariant(interface->mimeType());
            if (interface->glslDocument()->currentGlslVersion() >= 330)
                langVar |= GLSL::Lexer::Variant_GLSL_400;
            QStringList keywords = GLSL::Lexer::keywords(langVar);
//            m_keywordCompletions.clear();
            for (int index = 0; index < keywords.size(); ++index)
                m_completions << createCompletionItem(keywords.at(index), glslIcon(IconTypeKeyword));
//            m_keywordVariant = languageVariant(interface->mimeType());
//        }

  //      m_completions += m_keywordCompletions;
    }

    for (GLSL::Symbol *s : std::as_const(members)) {
        QIcon icon;
        GLSL::Variable *var = s->asVariable();
        if (var) {
            int storageType = var->qualifiers() & GLSL::QualifiedTypeAST::StorageMask;
            if (storageType == GLSL::QualifiedTypeAST::Attribute)
                icon = glslIcon(IconTypeAttribute);
            else if (storageType == GLSL::QualifiedTypeAST::Uniform)
                icon = glslIcon(IconTypeUniform);
            else if (storageType == GLSL::QualifiedTypeAST::Varying)
                icon = glslIcon(IconTypeVarying);
            else if (storageType == GLSL::QualifiedTypeAST::Const)
                icon = glslIcon(IconTypeConst);
            else
                icon = glslIcon(IconTypeVariable);
        } else if (s->asArgument()) {
            icon = glslIcon(IconTypeVariable);
        } else if (s->asFunction() || s->asOverloadSet()) {
            icon = glslIcon(IconTypeFunction);
        } else if (s->asStruct()) {
            icon = glslIcon(IconTypeType);
        } else {
            icon = glslIcon(IconTypeOther);
        }
        if (specialMembers.contains(s->name()))
            m_completions << createCompletionItem(s->name(), icon, SpecialMemberOrder);
        else
            m_completions << createCompletionItem(s->name(), icon);
    }

    m_startPosition = pos + 1;

    return new GenericProposal(m_startPosition, m_completions);
}

IAssistProposal *GlslCompletionAssistProcessor::createHintProposal(
    const QVector<GLSL::Function *> &symbols)
{
    FunctionHintProposalModelPtr model(new GlslFunctionHintProposalModel(symbols));
    IAssistProposal *proposal = new FunctionHintProposal(m_startPosition, model);
    return proposal;
}

bool GlslCompletionAssistProcessor::acceptsIdleEditor() const
{
    const int cursorPosition = interface()->position();
    const QChar ch = interface()->characterAt(cursorPosition - 1);

    const QChar characterUnderCursor = interface()->characterAt(cursorPosition);

    if (isIdentifierChar(ch) && (characterUnderCursor.isSpace() ||
                                 characterUnderCursor.isNull() ||
                                 isDelimiter(characterUnderCursor))) {
        int pos = interface()->position() - 1;
        for (; pos != -1; --pos) {
            if (! isIdentifierChar(interface()->characterAt(pos)))
                break;
        }
        ++pos;

        const QString word = interface()->textAt(pos, cursorPosition - pos);
        if (word.length() >= TextEditorSettings::completionSettings().m_characterThreshold
                && checkStartOfIdentifier(word)) {
            for (auto character : word) {
                if (!isIdentifierChar(character))
                    return false;
            }
            return true;
        }
    }

    return isActivationChar(ch);
}

// -----------------------------
// GlslCompletionAssistInterface
// -----------------------------
GlslCompletionAssistInterface::GlslCompletionAssistInterface(const QTextCursor &cursor,
                                                             const Utils::FilePath &fileName,
                                                             AssistReason reason,
                                                             const QString &mimeType,
                                                             const Document::Ptr &glslDoc)
    : AssistInterface(cursor, fileName, reason)
    , m_mimeType(mimeType)
    , m_glslDoc(glslDoc)
{
}

// GlslCompletionAssistProvider

class GlslCompletionAssistProvider : public TextEditor::CompletionAssistProvider
{
public:
    TextEditor::IAssistProcessor *createProcessor(const TextEditor::AssistInterface *) const override;

    int activationCharSequenceLength() const override;
    bool isActivationCharSequence(const QString &sequence) const override;
};

IAssistProcessor *GlslCompletionAssistProvider::createProcessor(const AssistInterface *) const
{
    return new GlslCompletionAssistProcessor;
}

int GlslCompletionAssistProvider::activationCharSequenceLength() const
{
    return 1;
}

bool GlslCompletionAssistProvider::isActivationCharSequence(const QString &sequence) const
{
    return isActivationChar(sequence.at(0));
}

CompletionAssistProvider *createGlslCompletionAssistProvider()
{
    return new GlslCompletionAssistProvider;
}

} // namespace Internal
} // namespace GlslEditor
