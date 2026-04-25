// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Asset/GetAssetDiffTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/DataTable.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialExpressionParameter.h"
#include "DiffUtils.h"
#include "Misc/PackagePath.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h"

FString UGetAssetDiffTool::GetToolDescription() const
{
	return TEXT("Get structured diff for binary Unreal assets (Blueprints, Materials, DataTables) against SCM base version. "
		"Returns JSON diff showing added/removed/modified elements. Supports Git and Perforce.");
}

TMap<FString, FBridgeSchemaProperty> UGetAssetDiffTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path (e.g., /Game/Blueprints/BP_Player) or absolute file path to the .uasset");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty ScmType;
	ScmType.Type = TEXT("string");
	ScmType.Description = TEXT("SCM type: 'git', 'perforce', or 'auto' (auto-detect). Default: 'auto'");
	ScmType.bRequired = false;
	ScmType.Enum = { TEXT("git"), TEXT("perforce"), TEXT("auto") };
	Schema.Add(TEXT("scm_type"), ScmType);

	FBridgeSchemaProperty BaseRevision;
	BaseRevision.Type = TEXT("string");
	BaseRevision.Description = TEXT("Base revision to compare against. Git: commit hash or 'HEAD' (default). Perforce: changelist number or '#have' (default).");
	BaseRevision.bRequired = false;
	Schema.Add(TEXT("base_revision"), BaseRevision);

	return Schema;
}

TArray<FString> UGetAssetDiffTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult UGetAssetDiffTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: asset_path"));
	}

	FString ScmTypeStr = GetStringArgOrDefault(Arguments, TEXT("scm_type"), TEXT("auto")).ToLower();
	FString BaseRevision = GetStringArgOrDefault(Arguments, TEXT("base_revision"), TEXT(""));

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("get-asset-diff: path='%s', scm='%s', revision='%s'"),
		*AssetPath, *ScmTypeStr, *BaseRevision);

	// Determine SCM type
	EScmType ScmType = EScmType::None;
	if (ScmTypeStr == TEXT("git"))
	{
		ScmType = EScmType::Git;
	}
	else if (ScmTypeStr == TEXT("perforce") || ScmTypeStr == TEXT("p4"))
	{
		ScmType = EScmType::Perforce;
	}
	else
	{
		ScmType = DetectScmType();
	}

	if (ScmType == EScmType::None)
	{
		return FBridgeToolResult::Error(TEXT("Could not detect SCM type. Specify 'scm_type' parameter."));
	}

	// Get the absolute file path
	FString FilePath = GetAssetFilePath(AssetPath);
	if (FilePath.IsEmpty())
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Could not resolve file path for: %s"), *AssetPath));
	}

	if (!FPaths::FileExists(FilePath))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Asset file not found: %s"), *FilePath));
	}

	// Load current asset
	UObject* CurrentAsset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!CurrentAsset)
	{
		// Try loading from file path if asset path didn't work
		FString LoadError;
		CurrentAsset = LoadAssetFromExternalPath(FilePath, LoadError);
		if (!CurrentAsset)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to load current asset: %s"), *LoadError));
		}
	}

	// Extract base version from SCM
	FString TempBasePath;
	FString ExtractError;
	bool bExtracted = false;

	if (ScmType == EScmType::Git)
	{
		if (BaseRevision.IsEmpty())
		{
			BaseRevision = TEXT("HEAD");
		}
		bExtracted = ExtractGitBaseVersion(FilePath, BaseRevision, TempBasePath, ExtractError);
	}
	else if (ScmType == EScmType::Perforce)
	{
		if (BaseRevision.IsEmpty())
		{
			BaseRevision = TEXT("#have");
		}
		bExtracted = ExtractPerforceBaseVersion(FilePath, BaseRevision, TempBasePath, ExtractError);
	}

	if (!bExtracted)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to extract base version: %s"), *ExtractError));
	}

	// Load base asset from temp file
	FString LoadError;
	UObject* BaseAsset = LoadAssetFromExternalPath(TempBasePath, LoadError);

	// Clean up temp file
	CleanupTempFile(TempBasePath);

	if (!BaseAsset)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to load base asset: %s"), *LoadError));
	}

	// Compare assets
	TSharedPtr<FJsonObject> DiffResult = CompareAssets(CurrentAsset, BaseAsset);
	if (!DiffResult.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("Failed to compare assets"));
	}

	// Add metadata
	DiffResult->SetStringField(TEXT("asset_path"), AssetPath);
	DiffResult->SetStringField(TEXT("file_path"), FilePath);
	DiffResult->SetStringField(TEXT("scm_type"), ScmType == EScmType::Git ? TEXT("git") : TEXT("perforce"));
	DiffResult->SetStringField(TEXT("base_revision"), BaseRevision);
	DiffResult->SetStringField(TEXT("asset_type"), CurrentAsset->GetClass()->GetName());

	return FBridgeToolResult::Json(DiffResult);
}

// === SCM Detection ===

UGetAssetDiffTool::EScmType UGetAssetDiffTool::DetectScmType() const
{
	FString ProjectDir = FPaths::ProjectDir();

	// Check for .git directory
	FString GitDir = FPaths::Combine(ProjectDir, TEXT(".git"));
	if (FPaths::DirectoryExists(GitDir))
	{
		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("Detected Git repository"));
		return EScmType::Git;
	}

	// Check for Perforce (P4CONFIG or .p4config)
	FString P4Config = FPaths::Combine(ProjectDir, TEXT("P4CONFIG"));
	FString P4ConfigDot = FPaths::Combine(ProjectDir, TEXT(".p4config"));
	if (FPaths::FileExists(P4Config) || FPaths::FileExists(P4ConfigDot))
	{
		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("Detected Perforce workspace"));
		return EScmType::Perforce;
	}

	// Check via ISourceControlModule
	ISourceControlModule& SourceControl = ISourceControlModule::Get();
	if (SourceControl.IsEnabled())
	{
		ISourceControlProvider& Provider = SourceControl.GetProvider();
		FString ProviderName = Provider.GetName().ToString();

		if (ProviderName.Contains(TEXT("Git")))
		{
			return EScmType::Git;
		}
		else if (ProviderName.Contains(TEXT("Perforce")))
		{
			return EScmType::Perforce;
		}
	}

	return EScmType::None;
}

FString UGetAssetDiffTool::GetAssetFilePath(const FString& AssetPath) const
{
	// If it's already an absolute path, return it
	if (FPaths::IsRelative(AssetPath) == false && FPaths::FileExists(AssetPath))
	{
		return AssetPath;
	}

	// Convert asset path to file path
	// /Game/Blueprints/BP_Player -> <ProjectDir>/Content/Blueprints/BP_Player.uasset
	FString RelativePath = AssetPath;

	// Remove leading /Game/ and replace with Content/
	if (RelativePath.StartsWith(TEXT("/Game/")))
	{
		RelativePath = RelativePath.Mid(6); // Remove "/Game/"
	}
	else if (RelativePath.StartsWith(TEXT("/Game")))
	{
		RelativePath = RelativePath.Mid(5); // Remove "/Game"
	}

	// Add .uasset extension if not present
	if (!RelativePath.EndsWith(TEXT(".uasset")) && !RelativePath.EndsWith(TEXT(".umap")))
	{
		RelativePath += TEXT(".uasset");
	}

	FString FilePath = FPaths::Combine(FPaths::ProjectContentDir(), RelativePath);
	FPaths::NormalizeFilename(FilePath);

	return FilePath;
}

// === Base Version Extraction ===

bool UGetAssetDiffTool::ExtractGitBaseVersion(const FString& FilePath, const FString& Revision, FString& OutTempPath, FString& OutError) const
{
	// Get path relative to git root
	FString ProjectDir = FPaths::ProjectDir();
	FString RelativePath = FilePath;
	FPaths::MakePathRelativeTo(RelativePath, *ProjectDir);
	RelativePath.ReplaceInline(TEXT("\\"), TEXT("/"));

	// Create temp file path
	FString TempDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MCP"), TEXT("Diff"));
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*TempDir);

	FString FileName = FPaths::GetBaseFilename(FilePath);
	FString Extension = FPaths::GetExtension(FilePath, true);
	OutTempPath = FPaths::Combine(TempDir, FString::Printf(TEXT("%s_base_%s%s"), *FileName, *FGuid::NewGuid().ToString().Left(8), *Extension));

	// Run git show to extract file content
	FString Command = FString::Printf(TEXT("git show %s:%s"), *Revision, *RelativePath);

	int32 ReturnCode = 0;
	FString StdOut;
	FString StdErr;

	// Use git to output to stdout and capture it
	// git show outputs binary, so we need to use git show with redirection
	FString FullCommand = FString::Printf(TEXT("git -C \"%s\" show \"%s:%s\""), *ProjectDir, *Revision, *RelativePath);

#if PLATFORM_WINDOWS
	// On Windows, use cmd /c to handle the redirection
	FString CmdExe = TEXT("cmd.exe");
	FString CmdArgs = FString::Printf(TEXT("/c \"cd /d \"%s\" && git show %s:\"%s\" > \"%s\"\""),
		*ProjectDir, *Revision, *RelativePath, *OutTempPath);

	void* PipeRead = nullptr;
	void* PipeWrite = nullptr;
	FPlatformProcess::CreatePipe(PipeRead, PipeWrite);

	FProcHandle ProcHandle = FPlatformProcess::CreateProc(
		*CmdExe, *CmdArgs, false, true, true, nullptr, 0, nullptr, PipeWrite);

	if (ProcHandle.IsValid())
	{
		FPlatformProcess::WaitForProc(ProcHandle);
		FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode);
		StdErr = FPlatformProcess::ReadPipe(PipeRead);
		FPlatformProcess::CloseProc(ProcHandle);
	}

	FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
#else
	// On other platforms, use shell redirection
	FString ShellCommand = FString::Printf(TEXT("cd \"%s\" && git show \"%s:%s\" > \"%s\""),
		*ProjectDir, *Revision, *RelativePath, *OutTempPath);
	FPlatformProcess::ExecProcess(TEXT("/bin/sh"), *FString::Printf(TEXT("-c \"%s\""), *ShellCommand), &ReturnCode, &StdOut, &StdErr);
#endif

	if (ReturnCode != 0 || !FPaths::FileExists(OutTempPath))
	{
		OutError = FString::Printf(TEXT("Git failed (code %d): %s"), ReturnCode, *StdErr);
		return false;
	}

	// Verify file was created and has content
	int64 FileSize = FPlatformFileManager::Get().GetPlatformFile().FileSize(*OutTempPath);
	if (FileSize <= 0)
	{
		OutError = TEXT("Git extracted empty file - file may not exist in the specified revision");
		return false;
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("Extracted Git base version to: %s (%lld bytes)"), *OutTempPath, FileSize);
	return true;
}

bool UGetAssetDiffTool::ExtractPerforceBaseVersion(const FString& FilePath, const FString& Revision, FString& OutTempPath, FString& OutError) const
{
	// Create temp file path
	FString TempDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MCP"), TEXT("Diff"));
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*TempDir);

	FString FileName = FPaths::GetBaseFilename(FilePath);
	FString Extension = FPaths::GetExtension(FilePath, true);
	OutTempPath = FPaths::Combine(TempDir, FString::Printf(TEXT("%s_base_%s%s"), *FileName, *FGuid::NewGuid().ToString().Left(8), *Extension));

	// Build p4 print command
	// p4 print -o <output> <file>#have  or  p4 print -o <output> <file>@<changelist>
	FString FileSpec = FilePath;
	if (Revision == TEXT("#have") || Revision.IsEmpty())
	{
		FileSpec += TEXT("#have");
	}
	else if (Revision.StartsWith(TEXT("#")))
	{
		FileSpec += Revision;
	}
	else if (Revision.StartsWith(TEXT("@")))
	{
		FileSpec += Revision;
	}
	else
	{
		// Assume it's a changelist number
		FileSpec += FString::Printf(TEXT("@%s"), *Revision);
	}

	int32 ReturnCode = 0;
	FString StdOut;
	FString StdErr;

#if PLATFORM_WINDOWS
	FString P4Exe = TEXT("p4.exe");
	FString P4Args = FString::Printf(TEXT("print -o \"%s\" \"%s\""), *OutTempPath, *FileSpec);

	FPlatformProcess::ExecProcess(*P4Exe, *P4Args, &ReturnCode, &StdOut, &StdErr);
#else
	FString P4Command = FString::Printf(TEXT("p4 print -o \"%s\" \"%s\""), *OutTempPath, *FileSpec);
	FPlatformProcess::ExecProcess(TEXT("/bin/sh"), *FString::Printf(TEXT("-c \"%s\""), *P4Command), &ReturnCode, &StdOut, &StdErr);
#endif

	if (ReturnCode != 0 || !FPaths::FileExists(OutTempPath))
	{
		OutError = FString::Printf(TEXT("Perforce failed (code %d): %s %s"), ReturnCode, *StdOut, *StdErr);
		return false;
	}

	// Verify file was created and has content
	int64 FileSize = FPlatformFileManager::Get().GetPlatformFile().FileSize(*OutTempPath);
	if (FileSize <= 0)
	{
		OutError = TEXT("Perforce extracted empty file - file may not exist in depot or revision");
		return false;
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("Extracted Perforce base version to: %s (%lld bytes)"), *OutTempPath, FileSize);
	return true;
}

// === Asset Loading ===

UObject* UGetAssetDiffTool::LoadAssetFromExternalPath(const FString& FilePath, FString& OutError) const
{
	// This is based on DiffUtils::LoadAssetFromExternalPath
	FPackagePath PackagePath;
	FString PathToUse = FilePath;

	if (!FPackagePath::TryFromPackageName(FilePath, PackagePath))
	{
		// Copy to temp directory with a clean name so it can be loaded
		FString CleanName = FPaths::GetBaseFilename(FilePath);
		// Remove characters that might cause issues
		for (const ANSICHAR Char : "#(){}[].")
		{
			CleanName.ReplaceCharInline(Char, TEXT('-'));
		}

		const FString Extension = TEXT(".") + FPaths::GetExtension(FilePath);
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectory(*FPaths::DiffDir());
		PathToUse = FPaths::CreateTempFilename(*FPaths::DiffDir(), *CleanName, *Extension);
		PathToUse = FPaths::ConvertRelativePathToFull(PathToUse);

		if (!FPlatformFileManager::Get().GetPlatformFile().CopyFile(*PathToUse, *FilePath))
		{
			OutError = FString::Printf(TEXT("Failed to copy file to temp: %s"), *FilePath);
			return nullptr;
		}

		PackagePath = FPackagePath::FromLocalPath(PathToUse);
	}

	if (PackagePath.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Invalid path: %s"), *FilePath);
		return nullptr;
	}

	// Use DiffUtils to load the package
	if (const UPackage* TempPackage = DiffUtils::LoadPackageForDiff(PackagePath, {}))
	{
		if (UObject* Object = TempPackage->FindAssetInPackage())
		{
			return Object;
		}
		OutError = TEXT("Package loaded but no asset found inside");
		return nullptr;
	}

	OutError = FString::Printf(TEXT("Failed to load package: %s"), *FilePath);
	return nullptr;
}

// === Diff Generation ===

TSharedPtr<FJsonObject> UGetAssetDiffTool::CompareAssets(UObject* CurrentAsset, UObject* BaseAsset) const
{
	if (!CurrentAsset || !BaseAsset)
	{
		return nullptr;
	}

	// Try Blueprint comparison
	UBlueprint* CurrentBP = Cast<UBlueprint>(CurrentAsset);
	UBlueprint* BaseBP = Cast<UBlueprint>(BaseAsset);
	if (CurrentBP && BaseBP)
	{
		return CompareBlueprints(CurrentBP, BaseBP);
	}

	// Try Material comparison
	UMaterial* CurrentMat = Cast<UMaterial>(CurrentAsset);
	UMaterial* BaseMat = Cast<UMaterial>(BaseAsset);
	if (CurrentMat && BaseMat)
	{
		return CompareMaterials(CurrentMat, BaseMat);
	}

	// Try Material Instance comparison
	UMaterialInstance* CurrentMatInst = Cast<UMaterialInstance>(CurrentAsset);
	UMaterialInstance* BaseMatInst = Cast<UMaterialInstance>(BaseAsset);
	if (CurrentMatInst && BaseMatInst)
	{
		return CompareMaterialInstances(CurrentMatInst, BaseMatInst);
	}

	// Try DataTable comparison
	UDataTable* CurrentDT = Cast<UDataTable>(CurrentAsset);
	UDataTable* BaseDT = Cast<UDataTable>(BaseAsset);
	if (CurrentDT && BaseDT)
	{
		return CompareDataTables(CurrentDT, BaseDT);
	}

	// Fall back to generic object comparison
	return CompareObjects(CurrentAsset, BaseAsset);
}

TSharedPtr<FJsonObject> UGetAssetDiffTool::CompareBlueprints(UBlueprint* Current, UBlueprint* Base) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonObject> Changes = MakeShareable(new FJsonObject);

	// Compare variables
	TSharedPtr<FJsonObject> VarChanges = CompareBlueprintVariables(Current, Base);
	if (VarChanges.IsValid())
	{
		Changes->SetObjectField(TEXT("variables"), VarChanges);
	}

	// Compare functions
	TSharedPtr<FJsonObject> FuncChanges = CompareBlueprintFunctions(Current, Base);
	if (FuncChanges.IsValid())
	{
		Changes->SetObjectField(TEXT("functions"), FuncChanges);
	}

	// Compare components
	TSharedPtr<FJsonObject> CompChanges = CompareBlueprintComponents(Current, Base);
	if (CompChanges.IsValid())
	{
		Changes->SetObjectField(TEXT("components"), CompChanges);
	}

	// Compare parent class
	FString CurrentParent = Current->ParentClass ? Current->ParentClass->GetName() : TEXT("None");
	FString BaseParent = Base->ParentClass ? Base->ParentClass->GetName() : TEXT("None");
	if (CurrentParent != BaseParent)
	{
		TSharedPtr<FJsonObject> ParentChange = MakeShareable(new FJsonObject);
		ParentChange->SetStringField(TEXT("old"), BaseParent);
		ParentChange->SetStringField(TEXT("new"), CurrentParent);
		Changes->SetObjectField(TEXT("parent_class"), ParentChange);
	}

	Result->SetObjectField(TEXT("changes"), Changes);

	// Summary
	int32 TotalChanges = 0;
	if (VarChanges.IsValid())
	{
		TotalChanges += VarChanges->GetArrayField(TEXT("added")).Num();
		TotalChanges += VarChanges->GetArrayField(TEXT("removed")).Num();
		TotalChanges += VarChanges->GetArrayField(TEXT("modified")).Num();
	}
	if (FuncChanges.IsValid())
	{
		TotalChanges += FuncChanges->GetArrayField(TEXT("added")).Num();
		TotalChanges += FuncChanges->GetArrayField(TEXT("removed")).Num();
	}
	if (CompChanges.IsValid())
	{
		TotalChanges += CompChanges->GetArrayField(TEXT("added")).Num();
		TotalChanges += CompChanges->GetArrayField(TEXT("removed")).Num();
	}

	Result->SetNumberField(TEXT("total_changes"), TotalChanges);
	Result->SetBoolField(TEXT("has_changes"), TotalChanges > 0);

	return Result;
}

TSharedPtr<FJsonObject> UGetAssetDiffTool::CompareBlueprintVariables(UBlueprint* Current, UBlueprint* Base) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> Added;
	TArray<TSharedPtr<FJsonValue>> Removed;
	TArray<TSharedPtr<FJsonValue>> Modified;

	// Build maps of variables by name
	TMap<FName, const FBPVariableDescription*> CurrentVars;
	TMap<FName, const FBPVariableDescription*> BaseVars;

	for (const FBPVariableDescription& Var : Current->NewVariables)
	{
		CurrentVars.Add(Var.VarName, &Var);
	}
	for (const FBPVariableDescription& Var : Base->NewVariables)
	{
		BaseVars.Add(Var.VarName, &Var);
	}

	// Find added and modified
	for (const auto& Pair : CurrentVars)
	{
		const FBPVariableDescription* CurrentVar = Pair.Value;
		const FBPVariableDescription** BaseVarPtr = BaseVars.Find(Pair.Key);

		if (!BaseVarPtr)
		{
			// Added
			TSharedPtr<FJsonObject> VarJson = MakeShareable(new FJsonObject);
			VarJson->SetStringField(TEXT("name"), CurrentVar->VarName.ToString());
			VarJson->SetStringField(TEXT("type"), CurrentVar->VarType.PinCategory.ToString());
			if (CurrentVar->VarType.PinSubCategoryObject.IsValid())
			{
				VarJson->SetStringField(TEXT("sub_type"), CurrentVar->VarType.PinSubCategoryObject->GetName());
			}
			Added.Add(MakeShareable(new FJsonValueObject(VarJson)));
		}
		else
		{
			// Check for modifications
			const FBPVariableDescription* BaseVar = *BaseVarPtr;
			TSharedPtr<FJsonObject> ModJson = MakeShareable(new FJsonObject);
			ModJson->SetStringField(TEXT("name"), CurrentVar->VarName.ToString());
			bool bModified = false;

			// Type change
			if (CurrentVar->VarType.PinCategory != BaseVar->VarType.PinCategory)
			{
				TSharedPtr<FJsonObject> TypeChange = MakeShareable(new FJsonObject);
				TypeChange->SetStringField(TEXT("old"), BaseVar->VarType.PinCategory.ToString());
				TypeChange->SetStringField(TEXT("new"), CurrentVar->VarType.PinCategory.ToString());
				ModJson->SetObjectField(TEXT("type"), TypeChange);
				bModified = true;
			}

			// Default value change
			if (CurrentVar->DefaultValue != BaseVar->DefaultValue)
			{
				TSharedPtr<FJsonObject> DefaultChange = MakeShareable(new FJsonObject);
				DefaultChange->SetStringField(TEXT("old"), BaseVar->DefaultValue);
				DefaultChange->SetStringField(TEXT("new"), CurrentVar->DefaultValue);
				ModJson->SetObjectField(TEXT("default_value"), DefaultChange);
				bModified = true;
			}

			// Replication change
			if (CurrentVar->RepNotifyFunc != BaseVar->RepNotifyFunc ||
				(CurrentVar->PropertyFlags & CPF_Net) != (BaseVar->PropertyFlags & CPF_Net))
			{
				bModified = true;
				ModJson->SetBoolField(TEXT("replication_changed"), true);
			}

			if (bModified)
			{
				Modified.Add(MakeShareable(new FJsonValueObject(ModJson)));
			}
		}
	}

	// Find removed
	for (const auto& Pair : BaseVars)
	{
		if (!CurrentVars.Contains(Pair.Key))
		{
			TSharedPtr<FJsonObject> VarJson = MakeShareable(new FJsonObject);
			VarJson->SetStringField(TEXT("name"), Pair.Value->VarName.ToString());
			VarJson->SetStringField(TEXT("type"), Pair.Value->VarType.PinCategory.ToString());
			Removed.Add(MakeShareable(new FJsonValueObject(VarJson)));
		}
	}

	Result->SetArrayField(TEXT("added"), Added);
	Result->SetArrayField(TEXT("removed"), Removed);
	Result->SetArrayField(TEXT("modified"), Modified);

	return Result;
}

TSharedPtr<FJsonObject> UGetAssetDiffTool::CompareBlueprintFunctions(UBlueprint* Current, UBlueprint* Base) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> Added;
	TArray<TSharedPtr<FJsonValue>> Removed;

	// Build sets of function names
	TSet<FString> CurrentFuncs;
	TSet<FString> BaseFuncs;

	for (UEdGraph* Graph : Current->FunctionGraphs)
	{
		if (Graph)
		{
			CurrentFuncs.Add(Graph->GetName());
		}
	}
	for (UEdGraph* Graph : Base->FunctionGraphs)
	{
		if (Graph)
		{
			BaseFuncs.Add(Graph->GetName());
		}
	}

	// Find added
	for (const FString& FuncName : CurrentFuncs)
	{
		if (!BaseFuncs.Contains(FuncName))
		{
			Added.Add(MakeShareable(new FJsonValueString(FuncName)));
		}
	}

	// Find removed
	for (const FString& FuncName : BaseFuncs)
	{
		if (!CurrentFuncs.Contains(FuncName))
		{
			Removed.Add(MakeShareable(new FJsonValueString(FuncName)));
		}
	}

	Result->SetArrayField(TEXT("added"), Added);
	Result->SetArrayField(TEXT("removed"), Removed);

	return Result;
}

TSharedPtr<FJsonObject> UGetAssetDiffTool::CompareBlueprintComponents(UBlueprint* Current, UBlueprint* Base) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> Added;
	TArray<TSharedPtr<FJsonValue>> Removed;

	// Build maps of components
	TMap<FName, USCS_Node*> CurrentComps;
	TMap<FName, USCS_Node*> BaseComps;

	if (Current->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Current->SimpleConstructionScript->GetAllNodes())
		{
			if (Node)
			{
				CurrentComps.Add(Node->GetVariableName(), Node);
			}
		}
	}
	if (Base->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Base->SimpleConstructionScript->GetAllNodes())
		{
			if (Node)
			{
				BaseComps.Add(Node->GetVariableName(), Node);
			}
		}
	}

	// Find added
	for (const auto& Pair : CurrentComps)
	{
		if (!BaseComps.Contains(Pair.Key))
		{
			TSharedPtr<FJsonObject> CompJson = MakeShareable(new FJsonObject);
			CompJson->SetStringField(TEXT("name"), Pair.Key.ToString());
			CompJson->SetStringField(TEXT("class"), Pair.Value->ComponentClass ? Pair.Value->ComponentClass->GetName() : TEXT("Unknown"));
			Added.Add(MakeShareable(new FJsonValueObject(CompJson)));
		}
	}

	// Find removed
	for (const auto& Pair : BaseComps)
	{
		if (!CurrentComps.Contains(Pair.Key))
		{
			TSharedPtr<FJsonObject> CompJson = MakeShareable(new FJsonObject);
			CompJson->SetStringField(TEXT("name"), Pair.Key.ToString());
			CompJson->SetStringField(TEXT("class"), Pair.Value->ComponentClass ? Pair.Value->ComponentClass->GetName() : TEXT("Unknown"));
			Removed.Add(MakeShareable(new FJsonValueObject(CompJson)));
		}
	}

	Result->SetArrayField(TEXT("added"), Added);
	Result->SetArrayField(TEXT("removed"), Removed);

	return Result;
}

TSharedPtr<FJsonObject> UGetAssetDiffTool::CompareMaterials(UMaterial* Current, UMaterial* Base) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonObject> Changes = MakeShareable(new FJsonObject);

	// Compare expression counts
	int32 CurrentExprCount = Current->GetExpressions().Num();
	int32 BaseExprCount = Base->GetExpressions().Num();

	if (CurrentExprCount != BaseExprCount)
	{
		TSharedPtr<FJsonObject> ExprChange = MakeShareable(new FJsonObject);
		ExprChange->SetNumberField(TEXT("old"), BaseExprCount);
		ExprChange->SetNumberField(TEXT("new"), CurrentExprCount);
		Changes->SetObjectField(TEXT("expression_count"), ExprChange);
	}

	// TODO: More detailed expression comparison would require matching by GUID

	Result->SetObjectField(TEXT("changes"), Changes);
	Result->SetBoolField(TEXT("has_changes"), CurrentExprCount != BaseExprCount);

	return Result;
}

TSharedPtr<FJsonObject> UGetAssetDiffTool::CompareMaterialInstances(UMaterialInstance* Current, UMaterialInstance* Base) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonObject> Changes = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> Modified;

	// Compare scalar parameters
	TArray<FMaterialParameterInfo> CurrentScalarParams;
	TArray<FMaterialParameterInfo> BaseScalarParams;
	TArray<FGuid> Guids;

	Current->GetAllScalarParameterInfo(CurrentScalarParams, Guids);
	Base->GetAllScalarParameterInfo(BaseScalarParams, Guids);

	for (const FMaterialParameterInfo& ParamInfo : CurrentScalarParams)
	{
		float CurrentValue = 0.0f;
		float BaseValue = 0.0f;
		Current->GetScalarParameterValue(ParamInfo, CurrentValue);
		Base->GetScalarParameterValue(ParamInfo, BaseValue);

		if (!FMath::IsNearlyEqual(CurrentValue, BaseValue))
		{
			TSharedPtr<FJsonObject> ParamChange = MakeShareable(new FJsonObject);
			ParamChange->SetStringField(TEXT("name"), ParamInfo.Name.ToString());
			ParamChange->SetStringField(TEXT("type"), TEXT("scalar"));
			ParamChange->SetNumberField(TEXT("old"), BaseValue);
			ParamChange->SetNumberField(TEXT("new"), CurrentValue);
			Modified.Add(MakeShareable(new FJsonValueObject(ParamChange)));
		}
	}

	// Compare vector parameters
	TArray<FMaterialParameterInfo> CurrentVectorParams;
	TArray<FMaterialParameterInfo> BaseVectorParams;

	Current->GetAllVectorParameterInfo(CurrentVectorParams, Guids);

	for (const FMaterialParameterInfo& ParamInfo : CurrentVectorParams)
	{
		FLinearColor CurrentValue;
		FLinearColor BaseValue;
		Current->GetVectorParameterValue(ParamInfo, CurrentValue);
		Base->GetVectorParameterValue(ParamInfo, BaseValue);

		if (!CurrentValue.Equals(BaseValue))
		{
			TSharedPtr<FJsonObject> ParamChange = MakeShareable(new FJsonObject);
			ParamChange->SetStringField(TEXT("name"), ParamInfo.Name.ToString());
			ParamChange->SetStringField(TEXT("type"), TEXT("vector"));
			ParamChange->SetStringField(TEXT("old"), BaseValue.ToString());
			ParamChange->SetStringField(TEXT("new"), CurrentValue.ToString());
			Modified.Add(MakeShareable(new FJsonValueObject(ParamChange)));
		}
	}

	Changes->SetArrayField(TEXT("parameters"), Modified);
	Result->SetObjectField(TEXT("changes"), Changes);
	Result->SetNumberField(TEXT("total_changes"), Modified.Num());
	Result->SetBoolField(TEXT("has_changes"), Modified.Num() > 0);

	return Result;
}

TSharedPtr<FJsonObject> UGetAssetDiffTool::CompareDataTables(UDataTable* Current, UDataTable* Base) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonObject> Changes = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> Added;
	TArray<TSharedPtr<FJsonValue>> Removed;
	TArray<TSharedPtr<FJsonValue>> Modified;

	// Get row names
	TArray<FName> CurrentRows = Current->GetRowNames();
	TArray<FName> BaseRows = Base->GetRowNames();

	TSet<FName> CurrentRowSet(CurrentRows);
	TSet<FName> BaseRowSet(BaseRows);

	const UScriptStruct* RowStruct = Current->GetRowStruct();

	// Find added rows
	for (const FName& RowName : CurrentRows)
	{
		if (!BaseRowSet.Contains(RowName))
		{
			TSharedPtr<FJsonObject> RowJson = MakeShareable(new FJsonObject);
			RowJson->SetStringField(TEXT("name"), RowName.ToString());

			// Include row data
			if (const uint8* RowPtr = Current->FindRowUnchecked(RowName))
			{
				FString RowData;
				RowStruct->ExportText(RowData, RowPtr, nullptr, nullptr, PPF_None, nullptr);
				RowJson->SetStringField(TEXT("data"), RowData);
			}

			Added.Add(MakeShareable(new FJsonValueObject(RowJson)));
		}
	}

	// Find removed rows
	for (const FName& RowName : BaseRows)
	{
		if (!CurrentRowSet.Contains(RowName))
		{
			TSharedPtr<FJsonObject> RowJson = MakeShareable(new FJsonObject);
			RowJson->SetStringField(TEXT("name"), RowName.ToString());
			Removed.Add(MakeShareable(new FJsonValueObject(RowJson)));
		}
	}

	// Find modified rows
	for (const FName& RowName : CurrentRows)
	{
		if (BaseRowSet.Contains(RowName))
		{
			const uint8* CurrentRowPtr = Current->FindRowUnchecked(RowName);
			const uint8* BaseRowPtr = Base->FindRowUnchecked(RowName);

			if (CurrentRowPtr && BaseRowPtr && RowStruct)
			{
				FString CurrentData, BaseData;
				RowStruct->ExportText(CurrentData, CurrentRowPtr, nullptr, nullptr, PPF_None, nullptr);
				RowStruct->ExportText(BaseData, BaseRowPtr, nullptr, nullptr, PPF_None, nullptr);

				if (CurrentData != BaseData)
				{
					TSharedPtr<FJsonObject> RowJson = MakeShareable(new FJsonObject);
					RowJson->SetStringField(TEXT("name"), RowName.ToString());
					RowJson->SetStringField(TEXT("old"), BaseData);
					RowJson->SetStringField(TEXT("new"), CurrentData);
					Modified.Add(MakeShareable(new FJsonValueObject(RowJson)));
				}
			}
		}
	}

	Changes->SetArrayField(TEXT("added"), Added);
	Changes->SetArrayField(TEXT("removed"), Removed);
	Changes->SetArrayField(TEXT("modified"), Modified);

	Result->SetObjectField(TEXT("changes"), Changes);
	Result->SetNumberField(TEXT("total_changes"), Added.Num() + Removed.Num() + Modified.Num());
	Result->SetBoolField(TEXT("has_changes"), Added.Num() + Removed.Num() + Modified.Num() > 0);

	return Result;
}

TSharedPtr<FJsonObject> UGetAssetDiffTool::CompareObjects(UObject* Current, UObject* Base) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonObject> Changes = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> Modified;

	// Compare properties via reflection
	for (TFieldIterator<FProperty> PropIt(Current->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property) continue;

		void* CurrentValuePtr = Property->ContainerPtrToValuePtr<void>(Current);
		void* BaseValuePtr = Property->ContainerPtrToValuePtr<void>(Base);

		if (!CurrentValuePtr || !BaseValuePtr) continue;

		FString CurrentValue = GetPropertyValueString(Property, CurrentValuePtr, Current);
		FString BaseValue = GetPropertyValueString(Property, BaseValuePtr, Base);

		if (CurrentValue != BaseValue)
		{
			TSharedPtr<FJsonObject> PropChange = MakeShareable(new FJsonObject);
			PropChange->SetStringField(TEXT("name"), Property->GetName());
			PropChange->SetStringField(TEXT("old"), BaseValue);
			PropChange->SetStringField(TEXT("new"), CurrentValue);
			Modified.Add(MakeShareable(new FJsonValueObject(PropChange)));
		}
	}

	Changes->SetArrayField(TEXT("properties"), Modified);
	Result->SetObjectField(TEXT("changes"), Changes);
	Result->SetNumberField(TEXT("total_changes"), Modified.Num());
	Result->SetBoolField(TEXT("has_changes"), Modified.Num() > 0);

	return Result;
}

// === Utility ===

FString UGetAssetDiffTool::GetPropertyValueString(FProperty* Property, void* Container, UObject* Owner) const
{
	if (!Property || !Container) return TEXT("");

	FString Value;
	Property->ExportText_Direct(Value, Container, Container, Owner, PPF_None);
	return Value;
}

void UGetAssetDiffTool::CleanupTempFile(const FString& TempPath) const
{
	if (!TempPath.IsEmpty() && FPaths::FileExists(TempPath))
	{
		FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*TempPath);
		UE_LOG(LogSoftUEBridgeEditor, Verbose, TEXT("Cleaned up temp file: %s"), *TempPath);
	}
}
