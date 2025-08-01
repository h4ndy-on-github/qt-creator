// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "projectpart.h"

#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>

#include <utils/algorithm.h>

#include <QFile>
#include <QDir>
#include <QTextStream>

using namespace ProjectExplorer;
using namespace Utils;

namespace CppEditor {

QString ProjectPart::id() const
{
    QString projectPartId = projectFileLocation();
    if (!displayName.isEmpty())
        projectPartId.append(QLatin1Char(' ') + displayName);
    return projectPartId;
}

QString ProjectPart::projectFileLocation() const
{
    QString location = projectFile.toUrlishString();
    if (projectFileLine > 0)
        location += ":" + QString::number(projectFileLine);
    if (projectFileColumn > 0)
        location += ":" + QString::number(projectFileColumn);
    return location;
}

bool ProjectPart::belongsToProject(const ProjectExplorer::Project *project) const
{
    return belongsToProject(project ? project->projectFilePath() : Utils::FilePath());
}

bool ProjectPart::belongsToProject(const Utils::FilePath &project) const
{
    return topLevelProject == project;
}

Project *ProjectPart::project() const
{
    return ProjectManager::projectWithProjectFilePath(topLevelProject);
}

QByteArray ProjectPart::readProjectConfigFile(const FilePath &projectConfigFile)
{
    QByteArray result;

    QFile f(projectConfigFile.toFSPathString());
    if (f.open(QIODevice::ReadOnly)) {
        QTextStream is(&f);
        result = is.readAll().toUtf8();
        f.close();
    }

    return result;
}

// TODO: Why do we keep the file *and* the resulting macros? Why do we read the file
//       in several places?
static Macros getProjectMacros(const RawProjectPart &rpp)
{
    Macros macros = rpp.projectMacros;
    if (!rpp.projectConfigFile.isEmpty())
        macros += Macro::toMacros(ProjectPart::readProjectConfigFile(rpp.projectConfigFile));
    return macros;
}

static HeaderPaths getHeaderPaths(const RawProjectPart &rpp,
                                  const RawProjectPartFlags &flags,
                                  const ToolchainInfo &tcInfo)
{
    HeaderPaths headerPaths;

    // Prevent duplicate include paths.
    // TODO: Do this once when finalizing the raw project part?
    std::set<FilePath> seenPaths;
    for (const HeaderPath &p : std::as_const(rpp.headerPaths)) {
        const FilePath cleanPath = p.path.cleanPath();
        if (seenPaths.insert(cleanPath).second)
            headerPaths << HeaderPath(cleanPath, p.type);
    }

    if (tcInfo.headerPathsRunner) {
        const HeaderPaths builtInHeaderPaths = tcInfo.headerPathsRunner(
                    flags.commandLineFlags, tcInfo.sysRootPath, tcInfo.targetTriple);
        for (const HeaderPath &header : builtInHeaderPaths) {
            // Prevent projects from adding built-in paths as user paths.
            erase_if(headerPaths, [&header](const HeaderPath &existing) {
                return header.path == existing.path;
            });
            headerPaths.push_back(HeaderPath{header.path, header.type});
        }
    }
    return headerPaths;
}

static Toolchain::MacroInspectionReport getToolchainMacros(
        const RawProjectPartFlags &flags, const ToolchainInfo &tcInfo, Utils::Language language)
{
    Toolchain::MacroInspectionReport report;
    if (tcInfo.macroInspectionRunner) {
        report = tcInfo.macroInspectionRunner(flags.commandLineFlags);
    } else if (language == Utils::Language::C) { // No compiler set in kit.
        report.languageVersion = Utils::LanguageVersion::LatestC;
    } else {
        report.languageVersion = Utils::LanguageVersion::LatestCxx;
    }
    return report;
}

static FilePaths getIncludedFiles(const RawProjectPart &rpp, const RawProjectPartFlags &flags)
{
    return !rpp.includedFiles.isEmpty() ? rpp.includedFiles : flags.includedFiles;
}

static QStringList getExtraCodeModelFlags(const RawProjectPart &rpp, const ProjectFiles &files)
{
    if (!Utils::anyOf(files, [](const ProjectFile &f) { return f.kind == ProjectFile::CudaSource; }))
        return {};

    Utils::FilePath cudaPath;
    for (const HeaderPath &hp : rpp.headerPaths) {
        if (hp.type == HeaderPathType::BuiltIn)
            continue;
        if (!hp.path.endsWith("/include"))
            continue;
        const FilePath includeDir = hp.path;
        if (!includeDir.pathAppended("cuda.h").exists())
            continue;
        for (FilePath dir = includeDir.parentDir(); cudaPath.isEmpty() && !dir.isRootPath();
             dir = dir.parentDir()) {
            if (dir.pathAppended("nvvm").exists())
                cudaPath = dir;
        }
        break;
    }
    if (!cudaPath.isEmpty())
        return {"--cuda-path=" + cudaPath.toUserOutput()};
    return {};
}

ProjectPart::ProjectPart(const Utils::FilePath &topLevelProject,
                         const RawProjectPart &rpp,
                         const QString &displayName,
                         const ProjectFiles &files,
                         Utils::Language language,
                         Utils::LanguageExtensions languageExtensions,
                         const RawProjectPartFlags &flags,
                         const ToolchainInfo &tcInfo)
    : topLevelProject(topLevelProject),
      displayName(displayName),
      projectFile(rpp.projectFile),
      projectConfigFile(rpp.projectConfigFile),
      projectFileLine(rpp.projectFileLine),
      projectFileColumn(rpp.projectFileColumn),
      callGroupId(rpp.callGroupId),
      language(language),
      languageExtensions(languageExtensions | flags.languageExtensions),
      qtVersion(rpp.qtVersion),
      files(files),
      includedFiles(getIncludedFiles(rpp, flags)),
      precompiledHeaders(rpp.precompiledHeaders),
      headerPaths(getHeaderPaths(rpp, flags, tcInfo)),
      projectMacros(getProjectMacros(rpp)),
      buildSystemTarget(rpp.buildSystemTarget),
      buildTargetType(rpp.buildTargetType),
      selectedForBuilding(rpp.selectedForBuilding),
      toolchainType(tcInfo.type),
      isMsvc2015Toolchain(tcInfo.isMsvc2015Toolchain),
      toolchainTargetTriple(tcInfo.targetTriple),
      targetTripleIsAuthoritative(tcInfo.targetTripleIsAuthoritative),
      toolchainAbi(tcInfo.abi),
      toolchainInstallDir(tcInfo.installDir),
      compilerFilePath(tcInfo.compilerFilePath),
      warningFlags(flags.warningFlags),
      extraCodeModelFlags(tcInfo.extraCodeModelFlags + getExtraCodeModelFlags(rpp, files)),
      compilerFlags(flags.commandLineFlags),
      m_macroReport(getToolchainMacros(flags, tcInfo, language)),
      languageFeatures(deriveLanguageFeatures())
{
}

CPlusPlus::LanguageFeatures ProjectPart::deriveLanguageFeatures() const
{
    const bool hasCxx = languageVersion >= Utils::LanguageVersion::CXX98;
    const bool hasQt = hasCxx && qtVersion != Utils::QtMajorVersion::None;
    CPlusPlus::LanguageFeatures features;
    features.cxx11Enabled = languageVersion >= Utils::LanguageVersion::CXX11;
    features.cxx14Enabled = languageVersion >= Utils::LanguageVersion::CXX14;
    features.cxx17Enabled = languageVersion >= Utils::LanguageVersion::CXX17;
    features.cxx20Enabled = languageVersion >= Utils::LanguageVersion::CXX20;
    features.cxxEnabled = hasCxx;
    features.c99Enabled = languageVersion >= Utils::LanguageVersion::C99;
    features.objCEnabled = languageExtensions.testFlag(Utils::LanguageExtension::ObjectiveC);
    features.qtEnabled = hasQt;
    features.qtMocRunEnabled = hasQt;
    if (!hasQt) {
        features.qtKeywordsEnabled = false;
    } else {
        features.qtKeywordsEnabled = !Utils::contains(projectMacros,
                [] (const ProjectExplorer::Macro &macro) { return macro.key == "QT_NO_KEYWORDS"; });
    }
    return features;
}

} // namespace CppEditor
