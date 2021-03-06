// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Misc/App.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "Runtime/Launch/Resources/Version.h"
#include "HAL/LowLevelMemTracker.h"
#include "BuildSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogApp, Log, All);

/* FApp static initialization
 *****************************************************************************/

#if UE_BUILD_DEVELOPMENT
bool FApp::bIsDebugGame = false;
#endif

FGuid FApp::InstanceId = FGuid::NewGuid();
FGuid FApp::SessionId = FGuid::NewGuid();
FString FApp::SessionName = FString();
FString FApp::SessionOwner = FString();
TArray<FString> FApp::SessionUsers = TArray<FString>();
bool FApp::Standalone = true;
bool FApp::bIsBenchmarking = false;
bool FApp::bUseFixedSeed = false;
bool FApp::bUseFixedTimeStep = false;
double FApp::FixedDeltaTime = 1 / 30.0;
double FApp::CurrentTime = 0.0;
double FApp::LastTime = 0.0;
double FApp::DeltaTime = 1 / 30.0;
double FApp::IdleTime = 0.0;
double FApp::IdleTimeOvershoot = 0.0;
FTimecode FApp::Timecode = FTimecode();
FFrameRate FApp::TimecodeFrameRate = FFrameRate(60,1);
float FApp::VolumeMultiplier = 1.0f;
float FApp::UnfocusedVolumeMultiplier = 0.0f;
bool FApp::bUseVRFocus = false;
bool FApp::bHasVRFocus = false;


/* FApp static interface
 *****************************************************************************/

FString FApp::GetBranchName()
{
	return FString(BuildSettings::GetBranchName());
}

const TCHAR* FApp::GetBuildVersion()
{
	return BuildSettings::GetBuildVersion();
}

int32 FApp::GetEngineIsPromotedBuild()
{
	return BuildSettings::IsPromotedBuild()? 1 : 0;
}


FString FApp::GetEpicProductIdentifier()
{
	return FString(TEXT(EPIC_PRODUCT_IDENTIFIER));
}

const TCHAR * FApp::GetDeploymentName()
{
	static TCHAR StaticDeploymentName[64] = {0};
	static bool bHaveDeployment = false;

	if (!bHaveDeployment)
	{
		// use -epicapp value from the commandline. Default deployment is not captured by this,
		// but this may not be a problem as that would be the case only during the development
		FParse::Value(FCommandLine::Get(), TEXT("EPICAPP="), StaticDeploymentName, ARRAY_COUNT(StaticDeploymentName) - 1);
		bHaveDeployment = true;
	}

	return StaticDeploymentName;
}

EBuildConfigurations::Type FApp::GetBuildConfiguration()
{
#if UE_BUILD_DEBUG
	return EBuildConfigurations::Debug;

#elif UE_BUILD_DEVELOPMENT
	return bIsDebugGame ? EBuildConfigurations::DebugGame : EBuildConfigurations::Development;

#elif UE_BUILD_SHIPPING
	return EBuildConfigurations::Shipping;

#elif UE_BUILD_TEST
	return EBuildConfigurations::Test;

#else
	return EBuildConfigurations::Unknown;
#endif
}

#if UE_BUILD_DEVELOPMENT
void FApp::SetDebugGame(bool bInIsDebugGame)
{
	bIsDebugGame = bInIsDebugGame;
}
#endif

FString FApp::GetBuildDate()
{
	return FString(ANSI_TO_TCHAR(__DATE__));
}


void FApp::InitializeSession()
{
	// parse session details on command line
	FString InstanceIdString;

	if (FParse::Value(FCommandLine::Get(), TEXT("-InstanceId="), InstanceIdString))
	{
		if (!FGuid::Parse(InstanceIdString, InstanceId))
		{
			UE_LOG(LogInit, Warning, TEXT("Invalid InstanceId on command line: %s"), *InstanceIdString);
		}
	}

	if (!InstanceId.IsValid())
	{
		InstanceId = FGuid::NewGuid();
	}

	FString SessionIdString;

	if (FParse::Value(FCommandLine::Get(), TEXT("-SessionId="), SessionIdString))
	{
		if (FGuid::Parse(SessionIdString, SessionId))
		{
			Standalone = false;
		}
		else
		{
			UE_LOG(LogInit, Warning, TEXT("Invalid SessionId on command line: %s"), *SessionIdString);
		}
	}

	FParse::Value(FCommandLine::Get(), TEXT("-SessionName="), SessionName);

	if (!FParse::Value(FCommandLine::Get(), TEXT("-SessionOwner="), SessionOwner))
	{
		SessionOwner = FPlatformProcess::UserName(false);
	}
}


bool FApp::IsInstalled()
{
	static int32 InstalledState = -1;

	if (InstalledState == -1)
	{
#if UE_BUILD_SHIPPING && PLATFORM_DESKTOP && !UE_SERVER
		bool bIsInstalled = true;
#else
		bool bIsInstalled = false;
#endif

#if PLATFORM_DESKTOP
		FString InstalledProjectBuildFile = FPaths::RootDir() / TEXT("Engine/Build/InstalledProjectBuild.txt");
		FPaths::NormalizeFilename(InstalledProjectBuildFile);
		bIsInstalled |= IFileManager::Get().FileExists(*InstalledProjectBuildFile);
#endif

		// Allow commandline options to disable/enable installed engine behavior
		if (bIsInstalled)
		{
			bIsInstalled = !FParse::Param(FCommandLine::Get(), TEXT("NotInstalled"));
		}
		else
		{
			bIsInstalled = FParse::Param(FCommandLine::Get(), TEXT("Installed"));
		}
		InstalledState = bIsInstalled ? 1 : 0;
	}
	return InstalledState == 1;
}


bool FApp::IsEngineInstalled()
{
	static int32 EngineInstalledState = -1;

	if (EngineInstalledState == -1)
	{
		bool bIsInstalledEngine = IsInstalled();

#if PLATFORM_DESKTOP
		FString InstalledBuildFile = FPaths::RootDir() / TEXT("Engine/Build/InstalledBuild.txt");
		FPaths::NormalizeFilename(InstalledBuildFile);
		bIsInstalledEngine |= IFileManager::Get().FileExists(*InstalledBuildFile);
#endif

		// Allow commandline options to disable/enable installed engine behavior
		if (bIsInstalledEngine)
		{
			bIsInstalledEngine = !FParse::Param(FCommandLine::Get(), TEXT("NotInstalledEngine"));
		}
		else
		{
			bIsInstalledEngine = FParse::Param(FCommandLine::Get(), TEXT("InstalledEngine"));
		}
		EngineInstalledState = bIsInstalledEngine ? 1 : 0;
	}

	return EngineInstalledState == 1;
}

bool FApp::IsEnterpriseInstalled()
{
	static int32 EnterpriseInstalledState = -1;

	if (EnterpriseInstalledState == -1)
	{
		bool bIsInstalledEnterprise = false;

#if PLATFORM_DESKTOP
		FString InstalledBuildFile = FPaths::RootDir() / TEXT("Enterprise/Build/InstalledBuild.txt");
		FPaths::NormalizeFilename(InstalledBuildFile);
		bIsInstalledEnterprise |= IFileManager::Get().FileExists(*InstalledBuildFile);
#endif

		// Allow commandline options to disable/enable installed engine behavior
		if (bIsInstalledEnterprise)
		{
			bIsInstalledEnterprise = !FParse::Param(FCommandLine::Get(), TEXT("NotInstalledEnterprise"));
		}
		else
		{
			bIsInstalledEnterprise = FParse::Param(FCommandLine::Get(), TEXT("InstalledEnterprise"));
		}
		EnterpriseInstalledState = bIsInstalledEnterprise ? 1 : 0;
	}

	return EnterpriseInstalledState == 1;
}

#if PLATFORM_WINDOWS && defined(__clang__)
bool FApp::IsUnattended() // @todo clang: Workaround for missing symbol export
{
	static bool bIsUnattended = FParse::Param(FCommandLine::Get(), TEXT("UNATTENDED"));
	return bIsUnattended || GIsAutomationTesting;
}
#endif

#if HAVE_RUNTIME_THREADING_SWITCHES
bool FApp::ShouldUseThreadingForPerformance()
{
	static bool OnlyOneThread = 
		FParse::Param(FCommandLine::Get(), TEXT("ONETHREAD")) ||
		FParse::Param(FCommandLine::Get(), TEXT("noperfthreads")) ||
		IsRunningDedicatedServer() ||
		!FPlatformProcess::SupportsMultithreading() ||
		FPlatformMisc::NumberOfCoresIncludingHyperthreads() < 4;
	return !OnlyOneThread;
}
#endif // HAVE_RUNTIME_THREADING_SWITCHES


static bool GUnfocusedVolumeMultiplierInitialised = false;
float FApp::GetUnfocusedVolumeMultiplier()
{
	if (!GUnfocusedVolumeMultiplierInitialised)
	{
		GUnfocusedVolumeMultiplierInitialised = true;
		GConfig->GetFloat(TEXT("Audio"), TEXT("UnfocusedVolumeMultiplier"), UnfocusedVolumeMultiplier, GEngineIni);
	}
	return UnfocusedVolumeMultiplier;
}

void FApp::SetUnfocusedVolumeMultiplier(float InVolumeMultiplier)
{
	UnfocusedVolumeMultiplier = InVolumeMultiplier;
	GConfig->SetFloat(TEXT("Audio"), TEXT("UnfocusedVolumeMultiplier"), UnfocusedVolumeMultiplier, GEngineIni);
	GUnfocusedVolumeMultiplierInitialised = true;
}

void FApp::SetUseVRFocus(bool bInUseVRFocus)
{
	UE_CLOG(bUseVRFocus != bInUseVRFocus, LogApp, Verbose, TEXT("UseVRFocus has changed to %d"), int(bInUseVRFocus));
	bUseVRFocus = bInUseVRFocus;
}

void FApp::SetHasVRFocus(bool bInHasVRFocus)
{
	UE_CLOG(bHasVRFocus != bInHasVRFocus, LogApp, Verbose, TEXT("HasVRFocus has changed to %d"), int(bInHasVRFocus));
	bHasVRFocus = bInHasVRFocus;
}
