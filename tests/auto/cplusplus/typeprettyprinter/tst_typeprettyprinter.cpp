// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <cplusplus/CoreTypes.h>
#include <cplusplus/FullySpecifiedType.h>
#include <cplusplus/Literals.h>
#include <cplusplus/Overview.h>
#include <cplusplus/Symbols.h>
#include <cplusplus/Type.h>
#include <cplusplus/TypePrettyPrinter.h>

#include <QObject>
#include <QTest>

//TESTED_COMPONENT=src/libs/cplusplus
using namespace CPlusPlus;

Q_DECLARE_METATYPE(CPlusPlus::FullySpecifiedType)
Q_DECLARE_METATYPE(Overview::StarBindFlags)

class tst_TypePrettyPrinter: public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void basic();
    void basic_data();
};

static const Identifier *nameId(const QString &name)
{ return new Identifier(name.toLatin1().constData(), name.toLatin1().size()); }

static Argument *arg(const QString &name, const FullySpecifiedType &ty)
{
    Argument *a = new Argument(0, 0, nameId(name));
    a->setType(ty);
    return a;
}

static TypenameArgument *typenameArg(const QString &name, bool isClassDeclarator)
{
    TypenameArgument *arg = new TypenameArgument(0, 0, nameId(name));
    arg->setClassDeclarator(isClassDeclarator);
    return arg;
}

static TemplateTypeArgument *tempTypeArg(const QString &name, const QString &conceptName)
{
    TemplateTypeArgument *arg = new TemplateTypeArgument(0, 0, nameId(name));
    arg->setConceptName(nameId(conceptName));
    return arg;
}

static Declaration *decl(const FullySpecifiedType &ty)
{
    Declaration *d = new Declaration(0, 0, nameId(""));
    d->setType(ty);
    return d;
}

static FullySpecifiedType voidTy()
{ return FullySpecifiedType(&VoidType::instance); }

static FullySpecifiedType intTy()
{ return FullySpecifiedType(new IntegerType(IntegerType::Int)); }

static FullySpecifiedType fnTy(const QString &name, const FullySpecifiedType &ret)
{
    Function *fn = new Function(0, 0, nameId(name));
    fn->setReturnType(ret);
    return FullySpecifiedType(fn);
}

static FullySpecifiedType fnTy(const QString &name, const FullySpecifiedType &ret,
                               const FullySpecifiedType &a0)
{
    Function *fn = new Function(0, 0, nameId(name));
    fn->setReturnType(ret);
    fn->addMember(arg("a0", a0));
    return FullySpecifiedType(fn);
}

static FullySpecifiedType fnTy(const QString &name, const FullySpecifiedType &ret,
                               const FullySpecifiedType &a0, const FullySpecifiedType &a1,
                               const FullySpecifiedType &a2)
{
    Function *fn = new Function(0, 0, nameId(name));
    fn->setReturnType(ret);
    fn->addMember(arg("a0", a0));
    fn->addMember(arg("a1", a1));
    fn->addMember(arg("a1", a2));
    return FullySpecifiedType(fn);
}

static FullySpecifiedType templTy(FullySpecifiedType declTy, bool isClassDeclarator)
{
    Template *templ = new Template(0, 0, nameId(""));
    templ->addMember(typenameArg("T", isClassDeclarator));
    templ->addMember(tempTypeArg("C", "ConceptType"));
    templ->addMember(decl(declTy));
    if (Function *func = declTy->asFunctionType())
        func->setEnclosingScope(templ);
    return FullySpecifiedType(templ);
}

static FullySpecifiedType refThis(const FullySpecifiedType &type)
{
    FullySpecifiedType result(type);
    Function *f = dynamic_cast<Function*>(result.type());
    Q_ASSERT(f);
    f->setRefQualifier(Function::LvalueRefQualifier);
    return result;
}

static FullySpecifiedType rrefThis(const FullySpecifiedType &type)
{
    FullySpecifiedType result(type);
    Function *function = dynamic_cast<Function*>(result.type());
    Q_ASSERT(function);
    function->setRefQualifier(Function::RvalueRefQualifier);
    return result;
}

static FullySpecifiedType cnstThis(const FullySpecifiedType &type)
{
    FullySpecifiedType result(type);
    Function *function = dynamic_cast<Function*>(result.type());
    Q_ASSERT(function);
    function->setConst(true);
    return result;
}

static FullySpecifiedType ptr(const FullySpecifiedType &el)
{ return FullySpecifiedType(new PointerType(el)); }

static FullySpecifiedType ref(const FullySpecifiedType &el)
{ return FullySpecifiedType(new ReferenceType(el, false)); }

static FullySpecifiedType rref(const FullySpecifiedType &el)
{ return FullySpecifiedType(new ReferenceType(el, true)); }

static FullySpecifiedType arr(const FullySpecifiedType &el)
{ return FullySpecifiedType(new ArrayType(el, 4)); }

static FullySpecifiedType cnst(const FullySpecifiedType &el)
{ FullySpecifiedType r(el); r.setConst(true); return r; }

static QString toString(Overview::StarBindFlags starBindFlags)
{
    QString result;
    if (starBindFlags & Overview::BindToIdentifier)
        result += QLatin1Char('n');
    if (starBindFlags & Overview::BindToTypeName)
        result += QLatin1Char('t');
    if (starBindFlags & Overview::BindToLeftSpecifier)
        result += QLatin1String("Sl");
    if (starBindFlags & Overview::BindToRightSpecifier)
        result += QLatin1String("Sr");
    return result;
}

static void addRow(const FullySpecifiedType &f, Overview::StarBindFlags starBindFlags,
            const QString &result, const QString &name = QString())
{
    const QString dataTag
        = QString::fromLatin1("\"%1\" with star binding \"%2\"").arg(result, toString(starBindFlags));
    QTest::newRow(dataTag.toLatin1().constData()) << f << starBindFlags << name << result;
}

void tst_TypePrettyPrinter::basic_data()
{
    QTest::addColumn<FullySpecifiedType>("type");
    QTest::addColumn<Overview::StarBindFlags>("starBindFlags");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("result");

    // Define some often used flag combinations.
    const Overview::StarBindFlags bindToNothing({});
    const Overview::StarBindFlags bindToBothSpecifiers
        = Overview::StarBindFlags(Overview::BindToLeftSpecifier | Overview::BindToRightSpecifier);
    const Overview::StarBindFlags bindToNameAndType
        = Overview::StarBindFlags(Overview::BindToIdentifier | Overview::BindToTypeName);
    const Overview::StarBindFlags bindToTypeAndRightSpecifier
        = Overview::StarBindFlags(Overview::BindToTypeName | Overview::BindToRightSpecifier);
    const Overview::StarBindFlags bindToAll = Overview::StarBindFlags(Overview::BindToIdentifier
        | Overview::BindToTypeName | Overview::BindToLeftSpecifier | Overview::BindToRightSpecifier);

    // The star bindings should not affect declarations without a star or reference sign.
    addRow(voidTy(), bindToNothing, "void");
    addRow(voidTy(), bindToAll, "void");
    addRow(voidTy(), bindToAll, "void foo", "foo");
    addRow(voidTy(), bindToNothing, "void foo", "foo");

    addRow(cnst(voidTy()), bindToNothing, "const void");
    addRow(cnst(voidTy()), bindToAll, "const void");
    addRow(cnst(voidTy()), bindToNothing, "const void foo", "foo");
    addRow(cnst(voidTy()), bindToAll, "const void foo", "foo");

    addRow(arr(voidTy()), bindToNothing, "void[]");
    addRow(arr(voidTy()), bindToAll, "void[]");
    addRow(arr(voidTy()), bindToNothing, "void foo[]", "foo");
    addRow(arr(voidTy()), bindToAll, "void foo[]", "foo");

    addRow(fnTy("foo", voidTy(), intTy()), bindToNothing, "void foo(int)", "foo");
    addRow(fnTy("foo", voidTy(), intTy()), bindToAll, "void foo(int)", "foo");
    addRow(refThis(fnTy("foo", voidTy())), bindToNothing, "void foo() &", "foo");
    addRow(rrefThis(fnTy("foo", voidTy())), bindToNothing, "void foo() &&", "foo");
    addRow(refThis(fnTy("foo", voidTy())), bindToAll, "void foo() &", "foo");
    addRow(rrefThis(fnTy("foo", voidTy())), bindToAll, "void foo() &&", "foo");
    addRow(cnstThis(refThis(fnTy("foo", voidTy()))), bindToNothing, "void foo() const &", "foo");
    addRow(cnstThis(rrefThis(fnTy("foo", voidTy()))), bindToNothing, "void foo() const &&", "foo");
    addRow(cnstThis(refThis(fnTy("foo", voidTy()))), bindToAll, "void foo() const&", "foo");
    addRow(cnstThis(rrefThis(fnTy("foo", voidTy()))), bindToAll, "void foo() const&&", "foo");

    // Pointers to functions and arrays are also excluded. It seems to be quite unusal to have
    // a space there.
    addRow(ptr(fnTy("foo", voidTy())), bindToAll, "void (*)()");
    addRow(ptr(fnTy("foo", voidTy())), bindToNothing, "void (*)()");
    addRow(ptr(fnTy("foo", voidTy())), bindToAll, "void (*foo)()", "foo");
    addRow(ptr(fnTy("foo", voidTy())), bindToNothing, "void (*foo)()", "foo");

    addRow(ptr(arr(voidTy())), bindToNothing, "void (*)[]");
    addRow(ptr(arr(voidTy())), bindToAll, "void (*)[]");

    addRow(ptr(arr(arr(voidTy()))), bindToNothing, "void (*)[][]");
    addRow(ptr(arr(arr(voidTy()))), bindToAll, "void (*)[][]");

    // Pointers only
    addRow(ptr(voidTy()), Overview::BindToTypeName, "void*");
    addRow(ptr(voidTy()), bindToAll, "void*");
    addRow(ptr(voidTy()), bindToNothing, "void *");
    addRow(ptr(voidTy()), Overview::BindToIdentifier, "void *foo", "foo");
    addRow(ptr(voidTy()), Overview::BindToTypeName, "void* foo", "foo");
    addRow(ptr(voidTy()), bindToNameAndType, "void*foo", "foo");
    addRow(ptr(voidTy()), bindToAll, "void*foo", "foo");
    addRow(ptr(voidTy()), bindToNothing, "void * foo", "foo");

    addRow(ptr(ptr(voidTy())), Overview::BindToTypeName, "void**");
    addRow(ptr(ptr(voidTy())), bindToAll, "void**");
    addRow(ptr(ptr(voidTy())), bindToNothing, "void **");
    addRow(ptr(ptr(voidTy())), Overview::BindToIdentifier, "void **foo", "foo");
    addRow(ptr(ptr(voidTy())), Overview::BindToTypeName, "void** foo", "foo");
    addRow(ptr(ptr(voidTy())), bindToNameAndType, "void**foo", "foo");
    addRow(ptr(ptr(voidTy())), bindToAll, "void**foo", "foo");
    addRow(ptr(ptr(voidTy())), bindToNothing, "void ** foo", "foo");

    addRow(ptr(cnst(voidTy())), bindToAll, "const void*");
    addRow(ptr(cnst(voidTy())), bindToNothing, "const void *");

    addRow(cnst(ptr(voidTy())), Overview::BindToIdentifier, "void * const foo", "foo");
    addRow(cnst(ptr(voidTy())), Overview::BindToTypeName, "void* const foo", "foo");
    addRow(cnst(ptr(voidTy())), Overview::BindToRightSpecifier, "void *const foo", "foo");
    addRow(cnst(ptr(voidTy())), bindToTypeAndRightSpecifier, "void*const foo", "foo");
    addRow(cnst(ptr(voidTy())), bindToAll, "void*const foo", "foo");
    addRow(cnst(ptr(voidTy())), bindToNothing, "void * const foo", "foo");

    addRow(cnst(ptr(cnst(voidTy()))), bindToAll, "const void*const");
    addRow(cnst(ptr(cnst(voidTy()))), Overview::BindToTypeName, "const void* const");
    addRow(cnst(ptr(cnst(voidTy()))), Overview::BindToRightSpecifier, "const void *const");
    addRow(cnst(ptr(cnst(voidTy()))), bindToNothing, "const void * const");

    addRow(cnst(ptr(voidTy())), Overview::BindToIdentifier, "void * const");
    addRow(cnst(ptr(voidTy())), Overview::BindToTypeName, "void* const");
    addRow(cnst(ptr(voidTy())), Overview::BindToRightSpecifier, "void *const");
    addRow(cnst(ptr(voidTy())), bindToTypeAndRightSpecifier, "void*const");
    addRow(cnst(ptr(voidTy())), bindToAll, "void*const");
    addRow(cnst(ptr(voidTy())), bindToNothing, "void * const");

    addRow(ptr(ptr(cnst(voidTy()))), Overview::BindToIdentifier, "const void **");
    addRow(ptr(cnst(ptr(cnst(voidTy())))), Overview::BindToIdentifier, "const void * const *");
    addRow(cnst(ptr(ptr(cnst(voidTy())))), Overview::BindToIdentifier, "const void ** const");

    addRow(ptr(cnst(ptr(voidTy()))), bindToNothing, "void * const *");
    addRow(ptr(cnst(ptr(voidTy()))), bindToAll, "void*const*");
    addRow(cnst(ptr(ptr(voidTy()))), bindToNothing, "void ** const");
    addRow(cnst(ptr(ptr(voidTy()))), bindToAll, "void**const");

    addRow(cnst(ptr(cnst(ptr(voidTy())))), bindToNothing, "void * const * const");
    addRow(cnst(ptr(cnst(ptr(voidTy())))), bindToAll, "void*const*const");
    addRow(cnst(ptr(ptr(cnst(ptr(voidTy()))))), bindToNothing, "void * const ** const");
    addRow(cnst(ptr(ptr(cnst(ptr(voidTy()))))), bindToAll, "void*const**const");

    addRow(cnst(ptr(cnst(ptr(cnst(voidTy()))))), Overview::BindToLeftSpecifier, "const void * const* const");
    addRow(cnst(ptr(cnst(ptr(cnst(voidTy()))))), Overview::BindToRightSpecifier, "const void *const *const");
    addRow(cnst(ptr(cnst(ptr(cnst(voidTy()))))), bindToBothSpecifiers, "const void *const*const");
    addRow(cnst(ptr(cnst(ptr(cnst(voidTy()))))), bindToNothing, "const void * const * const");
    addRow(cnst(ptr(cnst(ptr(cnst(voidTy()))))), bindToAll, "const void*const*const");

    // Pointers and arrays
    addRow(arr(ptr(voidTy())), bindToNothing, "void * argv[]", "argv");
    addRow(arr(ptr(voidTy())), bindToAll, "void*argv[]", "argv");

    addRow(arr(ptr(voidTy())), bindToNothing, "void *[]");
    addRow(arr(ptr(voidTy())), bindToAll, "void*[]");

    addRow(ptr(arr(ptr(voidTy()))), bindToNothing, "void *(*)[]");
    addRow(ptr(arr(ptr(voidTy()))), bindToAll, "void*(*)[]");

    addRow(arr(ptr(arr(voidTy()))), bindToNothing, "void (*[])[]");
    addRow(arr(ptr(arr(voidTy()))), bindToAll, "void (*[])[]");

    addRow(ptr(arr(voidTy())), bindToAll, "void (*foo)[]", "foo");
    addRow(ptr(arr(voidTy())), bindToNothing, "void (*foo)[]", "foo");

    // Pointers to functions
    addRow(ptr(fnTy("foo", voidTy(), intTy())), bindToNothing, "void (*foo)(int)", "foo");
    addRow(ptr(fnTy("foo", voidTy(), intTy())), bindToAll, "void (*foo)(int)", "foo");

    addRow(ptr(fnTy("foo", voidTy(), ptr(voidTy()))), bindToNothing, "void (*foo)(void *)", "foo");
    addRow(ptr(fnTy("foo", voidTy(), ptr(voidTy()))), bindToAll, "void (*foo)(void*)", "foo");

    // Pointers in more complex declarations
    FullySpecifiedType complexType1
        = ptr(fnTy("foo", voidTy(), intTy(), ptr(voidTy()), ptr(ptr(intTy()))));
    addRow(complexType1, bindToNothing, "void (*foo)(int, void *, int **)", "foo");
    addRow(complexType1, bindToAll, "void (*foo)(int, void*, int**)", "foo");

    FullySpecifiedType complexType2 = ptr(fnTy("foo", voidTy(),
        intTy(), cnst(ptr(cnst(voidTy()))), cnst(ptr(cnst(ptr(intTy()))))));
    addRow(complexType2, Overview::BindToLeftSpecifier,
           "void (*foo)(int, const void * const, int * const* const)", "foo");
    addRow(complexType2, Overview::BindToRightSpecifier,
           "void (*foo)(int, const void *const, int *const *const)", "foo");
    addRow(complexType2, bindToBothSpecifiers,
           "void (*foo)(int, const void *const, int *const*const)", "foo");
    addRow(complexType2, bindToNothing,
           "void (*foo)(int, const void * const, int * const * const)", "foo");
    addRow(complexType2, bindToAll,
           "void (*foo)(int, const void*const, int*const*const)", "foo");

    // References only
    addRow(ref(voidTy()), bindToNothing, "void &");
    addRow(ref(voidTy()), bindToAll, "void&");

    addRow(ref(ref(voidTy())), bindToNothing, "void & &");
    addRow(ref(ref(voidTy())), bindToAll, "void& &");

    addRow(ref(cnst(voidTy())), bindToNothing, "const void &");
    addRow(ref(cnst(voidTy())), bindToAll, "const void&");

    addRow(cnst(ref(cnst(voidTy()))), Overview::BindToRightSpecifier, "const void &const");
    addRow(cnst(ref(cnst(voidTy()))), Overview::BindToTypeName, "const void& const");
    addRow(cnst(ref(cnst(voidTy()))), bindToNothing, "const void & const");
    addRow(cnst(ref(cnst(voidTy()))), bindToAll, "const void&const");

    addRow(cnst(ref(voidTy())), Overview::BindToRightSpecifier, "void &const");
    addRow(cnst(ref(voidTy())), Overview::BindToTypeName, "void& const");
    addRow(cnst(ref(voidTy())), bindToNothing, "void & const");
    addRow(cnst(ref(voidTy())), bindToAll, "void&const");

    addRow(ref(ref(cnst(voidTy()))), bindToNothing, "const void & &");
    addRow(ref(ref(cnst(voidTy()))), bindToAll, "const void& &");

    addRow(ref(cnst(ref(cnst(voidTy())))), Overview::BindToTypeName, "const void& const &");
    addRow(ref(cnst(ref(cnst(voidTy())))), Overview::BindToLeftSpecifier, "const void & const&");
    addRow(ref(cnst(ref(cnst(voidTy())))), Overview::BindToRightSpecifier, "const void &const &");
    addRow(ref(cnst(ref(cnst(voidTy())))), bindToBothSpecifiers, "const void &const&");
    addRow(ref(cnst(ref(cnst(voidTy())))), bindToNothing, "const void & const &");
    addRow(ref(cnst(ref(cnst(voidTy())))), bindToAll, "const void&const&");

    addRow(cnst(ref(ref(cnst(voidTy())))), bindToBothSpecifiers, "const void & &const");
    addRow(cnst(ref(ref(cnst(voidTy())))), bindToNothing, "const void & & const");
    addRow(cnst(ref(ref(cnst(voidTy())))), bindToAll, "const void& &const");

    addRow(cnst(ref(cnst(ref(cnst(voidTy()))))), Overview::BindToLeftSpecifier, "const void & const& const");
    addRow(cnst(ref(cnst(ref(cnst(voidTy()))))), Overview::BindToRightSpecifier, "const void &const &const");
    addRow(cnst(ref(cnst(ref(cnst(voidTy()))))), bindToBothSpecifiers, "const void &const&const");
    addRow(cnst(ref(cnst(ref(cnst(voidTy()))))), bindToNothing, "const void & const & const");
    addRow(cnst(ref(cnst(ref(cnst(voidTy()))))), bindToAll, "const void&const&const");

    addRow(ref(cnst(ref(voidTy()))), bindToNothing, "void & const &");
    addRow(ref(cnst(ref(voidTy()))), bindToAll, "void&const&");

    addRow(cnst(ref(ref(voidTy()))), bindToNothing, "void & & const");
    addRow(cnst(ref(ref(voidTy()))), bindToAll, "void& &const");

    addRow(cnst(ref(cnst(ref(voidTy())))), Overview::BindToLeftSpecifier, "void & const& const");
    addRow(cnst(ref(cnst(ref(voidTy())))), Overview::BindToRightSpecifier, "void &const &const");
    addRow(cnst(ref(cnst(ref(voidTy())))), bindToBothSpecifiers, "void &const&const");
    addRow(cnst(ref(cnst(ref(voidTy())))), bindToNothing, "void & const & const");
    addRow(cnst(ref(cnst(ref(voidTy())))), bindToAll, "void&const&const");

    addRow(ref(fnTy("foo", voidTy())), bindToNothing, "void (&foo)()", "foo");
    addRow(ref(fnTy("foo", voidTy())), bindToAll, "void (&foo)()", "foo");

    addRow(arr(ref(voidTy())), bindToNothing, "void &[]");
    addRow(arr(ref(voidTy())), bindToAll, "void&[]");

    addRow(ref(arr(voidTy())), bindToNothing, "void (&)[]");
    addRow(ref(arr(voidTy())), bindToAll, "void (&)[]");

    addRow(ref(arr(arr(voidTy()))), bindToNothing, "void (&)[][]");
    addRow(ref(arr(arr(voidTy()))), bindToAll, "void (&)[][]");

    addRow(ref(arr(ref(voidTy()))), bindToNothing, "void &(&)[]");
    addRow(ref(arr(ref(voidTy()))), bindToAll, "void&(&)[]");

    addRow(arr(ref(arr(voidTy()))), bindToNothing, "void (&[])[]");
    addRow(arr(ref(arr(voidTy()))), bindToAll, "void (&[])[]");

    // Rvalue References
    addRow(rref(voidTy()), bindToNothing, "void &&");
    addRow(rref(voidTy()), bindToAll, "void&&");

    addRow(rref(cnst(voidTy())), bindToNothing, "const void &&");
    addRow(rref(cnst(voidTy())), bindToAll, "const void&&");

    addRow(rref(cnst(voidTy())), bindToNothing, "const void && foo", "foo");
    addRow(rref(cnst(voidTy())), bindToAll, "const void&&foo", "foo");

    addRow(rref(arr(voidTy())), bindToNothing, "void (&&)[]");
    addRow(rref(arr(voidTy())), bindToAll, "void (&&)[]");

    addRow(rref(arr(arr(voidTy()))), bindToNothing, "void (&&)[][]");
    addRow(rref(arr(arr(voidTy()))), bindToAll, "void (&&)[][]");

    // Pointers and references mixed
    addRow(cnst(ref(ptr(voidTy()))), bindToBothSpecifiers, "void *&const");
    addRow(cnst(ref(ptr(voidTy()))), bindToNothing, "void *& const");
    addRow(cnst(ref(ptr(voidTy()))), bindToAll, "void*&const");

    // Functions with pointer parameters
    addRow(fnTy("foo", voidTy(), ptr(voidTy())), bindToNothing, "void foo(void *)", "foo");
    addRow(fnTy("foo", voidTy(), ptr(voidTy())), bindToAll, "void foo(void*)", "foo");

    // Functions with pointer or reference returns
    addRow(ptr(fnTy("foo", ptr(voidTy()), intTy())), bindToNothing, "void * (*foo)(int)", "foo");
    addRow(ptr(fnTy("foo", ptr(voidTy()), intTy())), Overview::BindToTypeName, "void* (*foo)(int)", "foo");
    addRow(ptr(fnTy("foo", ptr(voidTy()), intTy())), bindToAll, "void*(*foo)(int)", "foo");

    addRow(ptr(fnTy("foo", ref(voidTy()), ptr(voidTy()))), bindToNothing, "void & (*foo)(void *)", "foo");
    addRow(ptr(fnTy("foo", ref(voidTy()), ptr(voidTy()))), Overview::BindToTypeName, "void& (*foo)(void*)", "foo");
    addRow(ptr(fnTy("foo", ref(voidTy()), ptr(voidTy()))), bindToAll, "void&(*foo)(void*)", "foo");

    addRow(fnTy("foo", ptr(voidTy()), intTy()), bindToNothing, "void * foo(int)", "foo");
    addRow(fnTy("foo", ptr(voidTy()), intTy()), Overview::BindToTypeName, "void* foo(int)", "foo");
    addRow(fnTy("foo", ptr(voidTy()), intTy()), bindToAll, "void*foo(int)", "foo");

    addRow(fnTy("foo", ref(voidTy()), ptr(voidTy())), bindToNothing, "void & foo(void *)", "foo");
    addRow(fnTy("foo", ref(voidTy()), ptr(voidTy())), Overview::BindToTypeName, "void& foo(void*)", "foo");
    addRow(fnTy("foo", ref(voidTy()), ptr(voidTy())), bindToAll, "void&foo(void*)", "foo");

    addRow(templTy(fnTy("foo", voidTy(), voidTy()), true), bindToNothing, "template<class T, ConceptType C>\nvoid foo()", "foo");
    addRow(templTy(fnTy("foo", voidTy(), voidTy()), false), bindToNothing, "template<typename T, ConceptType C>\nvoid foo()", "foo");
}

void tst_TypePrettyPrinter::basic()
{
    QFETCH(FullySpecifiedType, type);
    QFETCH(Overview::StarBindFlags, starBindFlags);
    QFETCH(QString, name);
    QFETCH(QString, result);

    Overview o;
    o.showReturnTypes = true;
    o.showEnclosingTemplate = true;
    o.starBindFlags = starBindFlags;
    TypePrettyPrinter pp(&o);
    QCOMPARE(pp(type, name), result);
}

QTEST_APPLESS_MAIN(tst_TypePrettyPrinter)
#include "tst_typeprettyprinter.moc"
