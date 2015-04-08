// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved. 
#include "ScriptEditorPluginPrivatePCH.h"
#include "ScriptBlueprintGeneratedClass.h"

UScriptFactory::UScriptFactory(const FObjectInitializer& ObjectInitializer)
	: Super( ObjectInitializer )
{
	SupportedClass = UScriptBlueprint::StaticClass();
	ParentClass = AActor::StaticClass();

	FScriptContextBase::GetSupportedScriptFileFormats(Formats);

	bCreateNew = false;
	bEditorImport = true;
	bText = true;
	bEditAfterNew = true;	
}

bool UScriptFactory::ConfigureProperties()
{
	// Null the parent class so we can check for selection later
	ParentClass = NULL;

	// Load the class viewer module to display a class picker
	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

	// Fill in options
	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.DisplayMode = EClassViewerDisplayMode::TreeView;
	Options.bShowObjectRootClass = true;
	Options.bIsBlueprintBaseOnly = true;
	Options.bShowUnloadedBlueprints = true;

	const FText TitleText = NSLOCTEXT("EditorFactories", "CreateScriptOptions", "Pick Parent Class");
	UClass* ChosenClass = NULL;
	const bool bPressedOk = SClassPickerDialog::PickClass(TitleText, Options, ChosenClass, UScriptBlueprintGeneratedClass::StaticClass());
	if (bPressedOk)
	{
		ParentClass = ChosenClass;
	}

	return bPressedOk;
}


bool UScriptFactory::DoesSupportClass(UClass* Class)
{
	return Class == UScriptBlueprint::StaticClass();
}

UObject* UScriptFactory::FactoryCreateText(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const TCHAR*& Buffer, const TCHAR* BufferEnd, FFeedbackContext* Warn)
{
	GEditor->SelectNone(true, true, false);

	UScriptBlueprint* NewBlueprint = nullptr;
	TAutoPtr<FScriptContextBase> ScriptContext(FScriptContextBase::CreateContext(Buffer, nullptr, nullptr));
	if (ScriptContext.IsValid())
	{
	    UScriptBlueprint* NewBlueprint = Cast<UScriptBlueprint>( FindObject<UBlueprint>( InParent, *InName.ToString() ) );
	    if( NewBlueprint != nullptr )
	    {
		    NewBlueprint->Modify();
	    }
	    else
	    {
		    NewBlueprint = CastChecked<UScriptBlueprint>(FKismetEditorUtilities::CreateBlueprint(ParentClass, InParent, InName, BPTYPE_Normal, UScriptBlueprint::StaticClass(), UScriptBlueprintGeneratedClass::StaticClass(), "UScriptFactory"));
		}
		NewBlueprint->SourceFilePath = FReimportManager::SanitizeImportFilename(CurrentFilename, NewBlueprint);
		NewBlueprint->SourceCode = Buffer;
		NewBlueprint->SourceFileTimestamp = IFileManager::Get().GetTimeStamp(*NewBlueprint->SourceFilePath).ToString();

		// Need to make sure we compile with the new source code
		FKismetEditorUtilities::CompileBlueprint(NewBlueprint);

		FEditorDelegates::OnAssetPostImport.Broadcast(this, NewBlueprint);
	}
	else
	{
		UE_LOG(LogScriptEditorPlugin, Warning, TEXT("Failed to import %s: could not compile script."), *CurrentFilename);
	}

	return NewBlueprint;
}


/** UReimportScriptFactory */

UReimportScriptFactory::UReimportScriptFactory(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

bool UReimportScriptFactory::ConfigureProperties()
{
	return UFactory::ConfigureProperties();
}

bool UReimportScriptFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	UScriptBlueprint* ScriptClass = Cast<UScriptBlueprint>(Obj);
	if (ScriptClass)
	{
		OutFilenames.Add(FReimportManager::ResolveImportFilename(ScriptClass->SourceFilePath, ScriptClass));
		return true;
	}
	return false;
}

void UReimportScriptFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UScriptBlueprint* ScriptClass = Cast<UScriptBlueprint>(Obj);
	if (ScriptClass && ensure(NewReimportPaths.Num() == 1))
	{
		ScriptClass->SourceFilePath = FReimportManager::SanitizeImportFilename(NewReimportPaths[0], Obj);
	}
}

/**
* Reimports specified texture from its source material, if the meta-data exists
*/
EReimportResult::Type UReimportScriptFactory::Reimport(UObject* Obj)
{
	UScriptBlueprint* ScriptClass = Cast<UScriptBlueprint>(Obj);
	if (!ScriptClass)
	{
		return EReimportResult::Failed;
	}

	TGuardValue<UScriptBlueprint*> OriginalScriptGuardValue(OriginalScript, ScriptClass);

	const FString ResolvedSourceFilePath = FReimportManager::ResolveImportFilename(ScriptClass->SourceFilePath, ScriptClass);
	if (!ResolvedSourceFilePath.Len())
	{
		return EReimportResult::Failed;
	}

	UE_LOG(LogScriptEditorPlugin, Log, TEXT("Performing atomic reimport of [%s]"), *ResolvedSourceFilePath);

	// Ensure that the file provided by the path exists
	if (IFileManager::Get().FileSize(*ResolvedSourceFilePath) == INDEX_NONE)
	{
		UE_LOG(LogScriptEditorPlugin, Warning, TEXT("Cannot reimport: source file cannot be found."));
		return EReimportResult::Failed;
	}

	if (UFactory::StaticImportObject(ScriptClass->GetClass(), ScriptClass->GetOuter(), *ScriptClass->GetName(), RF_Public | RF_Standalone, *ResolvedSourceFilePath, NULL, this))
	{
		UE_LOG(LogScriptEditorPlugin, Log, TEXT("Imported successfully"));
		// Try to find the outer package so we can dirty it up
		if (ScriptClass->GetOuter())
		{
			ScriptClass->GetOuter()->MarkPackageDirty();
		}
		else
		{
			ScriptClass->MarkPackageDirty();
		}
	}
	else
	{
		UE_LOG(LogScriptEditorPlugin, Warning, TEXT("-- import failed"));
	}

	return EReimportResult::Succeeded;
}

int32 UReimportScriptFactory::GetPriority() const
{
	return ImportPriority;
}
