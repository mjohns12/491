// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "VSAccessorPrivatePCH.h"
#include "ModuleManager.h"


#include "AllowWindowsPlatformTypes.h"
#if VSACCESSOR_HAS_DTE
	#pragma warning(disable: 4278)
	#pragma warning(disable: 4146)
	#pragma warning(disable: 4191)

	// atltransactionmanager.h doesn't use the W equivalent functions, use this workaround
	#ifndef DeleteFile
		#define DeleteFile DeleteFileW
	#endif
	#ifndef MoveFile
		#define MoveFile MoveFileW
	#endif
	#include "atlbase.h"
	#undef DeleteFile
	#undef MoveFile

	// import EnvDTE
	#import "libid:80cc9f66-e7d8-4ddd-85b6-d9e6cd0e93e2" version("8.0") lcid("0") raw_interfaces_only named_guids

	#pragma warning(default: 4191)
	#pragma warning(default: 4146)
	#pragma warning(default: 4278)
#endif
#include <tlhelp32.h>
#include <wbemidl.h>
#pragma comment(lib, "wbemuuid.lib")
#include "HideWindowsPlatformTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogVSAccessor, Log, All);

IMPLEMENT_MODULE( FVSAccessorModule, VSAccessor );

/** The VS query in progress message */
TWeakPtr<class SNotificationItem> VSNotificationPtr = NULL;

/** Return codes when trying to access an existing VS instance */
enum class EAccessVisualStudioResult : uint8
{
	/** An instance of Visual Studio is available, and the relevant output variables have been filled in */
	VSInstanceIsOpen,
	/** An instance of Visual Studio is not available */
	VSInstanceIsNotOpen,
	/** It is unknown whether an instance of Visual Studio is available, as an error occurred when performing the check */
	VSInstanceUnknown,
};

/** save all open documents in visual studio, when recompiling */
void SaveVisualStudioDocuments()
{
	FVSAccessorModule& VSAccessorModule = FModuleManager::LoadModuleChecked<FVSAccessorModule>(TEXT("VSAccessor"));
	VSAccessorModule.SaveAllOpenVisualStudioDocuments();
}

void FVSAccessorModule::StartupModule()
{
	VSLaunchTime = 0.0;

	// Setup compilation for saving all VS documents upon compilation start
	FModuleManager::Get().OnModuleCompilerStarted().AddStatic( &SaveVisualStudioDocuments );

	// Cache this so we don't have to do it on a background thread
	SolutionPath = FPaths::ConvertRelativePathToFull(FModuleManager::Get().GetSolutionFilepath());

	// Preferential order of VS versions
	AddVisualStudioVersion(12); // Visual Studio 2013
	if (!FRocketSupport::IsRocket())
	{
		AddVisualStudioVersion(11); // Visual Studio 2012
	}
}

void FVSAccessorModule::ShutdownModule()
{
	FModuleManager::Get().OnModuleCompilerStarted().RemoveStatic( &SaveVisualStudioDocuments );
}

#if VSACCESSOR_HAS_DTE

bool IsVisualStudioDTEMoniker(const FString& InName, const TArray<FVSAccessorModule::VisualStudioLocation>& InLocations)
{
	for(int32 VSVersion = 0; VSVersion < InLocations.Num(); VSVersion++)
	{
		if(InName.StartsWith(InLocations[VSVersion].ROTMoniker))
		{
			return true;
		}
	}

	return false;
}

/** Accesses the correct visual studio instance if possible. */
EAccessVisualStudioResult AccessVisualStudioViaDTE(CComPtr<EnvDTE::_DTE>& OutDTE, const FString& InSolutionPath, const TArray<FVSAccessorModule::VisualStudioLocation>& InLocations)
{
	EAccessVisualStudioResult AccessResult = EAccessVisualStudioResult::VSInstanceIsNotOpen;

	// Open the Running Object Table (ROT)
	IRunningObjectTable* RunningObjectTable;
	if (SUCCEEDED(GetRunningObjectTable(0, &RunningObjectTable)) && RunningObjectTable)
	{
		IEnumMoniker* MonikersTable;
		if (SUCCEEDED(RunningObjectTable->EnumRunning(&MonikersTable)))
		{
			MonikersTable->Reset();

			// Look for all visual studio instances in the ROT
			IMoniker* CurrentMoniker;
			while (AccessResult != EAccessVisualStudioResult::VSInstanceIsOpen && MonikersTable->Next(1, &CurrentMoniker, NULL) == S_OK)
			{
				IBindCtx* BindContext;
				LPOLESTR OutName;
				if (SUCCEEDED(CreateBindCtx(0, &BindContext)) && SUCCEEDED(CurrentMoniker->GetDisplayName(BindContext, NULL, &OutName)))
				{
					if (IsVisualStudioDTEMoniker(FString(OutName), InLocations))
					{
						CComPtr<IUnknown> ComObject;
						if (SUCCEEDED(RunningObjectTable->GetObject(CurrentMoniker, &ComObject)))
						{
							CComPtr<EnvDTE::_DTE> TempDTE;
							TempDTE = ComObject;

							// Get the solution path for this instance
							// If it equals the solution we would have opened above in RunVisualStudio(), we'll take that
							CComPtr<EnvDTE::_Solution> Solution;
							LPOLESTR OutPath;
							if (SUCCEEDED(TempDTE->get_Solution(&Solution)) &&
								SUCCEEDED(Solution->get_FullName(&OutPath)))
							{
								FString Filename(OutPath);
								FPaths::NormalizeFilename(Filename);

								if( Filename == InSolutionPath )
								{
									OutDTE = TempDTE;
									AccessResult = EAccessVisualStudioResult::VSInstanceIsOpen;
								}
							}
						}
						else
						{
							UE_LOG(LogVSAccessor, Warning, TEXT("Couldn't get Visual Studio COM object"));
							AccessResult = EAccessVisualStudioResult::VSInstanceUnknown;
						}
					}
				}
				else
				{
					UE_LOG(LogVSAccessor, Warning, TEXT("Couldn't get display name"));
					AccessResult = EAccessVisualStudioResult::VSInstanceUnknown;
				}
				BindContext->Release();
				CurrentMoniker->Release();
			}
			MonikersTable->Release();
		}
		else
		{
			UE_LOG(LogVSAccessor, Warning, TEXT("Couldn't enumerate ROT table"));
			AccessResult = EAccessVisualStudioResult::VSInstanceUnknown;
		}
		RunningObjectTable->Release();
	}
	else
	{
		UE_LOG(LogVSAccessor, Warning, TEXT("Couldn't get ROT table"));
		AccessResult = EAccessVisualStudioResult::VSInstanceUnknown;
	}

	return AccessResult;
}

bool FVSAccessorModule::OpenVisualStudioSolutionViaDTE()
{
	// Initialize the com library, if not already by this thread
	if (!FWindowsPlatformMisc::CoInitialize())
	{
		UE_LOG(LogVSAccessor, Error, TEXT( "ERROR - Could not initialize COM library!" ));
		return false;
	}

	bool bSuccess = false;

	CComPtr<EnvDTE::_DTE> DTE;
	switch (AccessVisualStudioViaDTE(DTE, SolutionPath, Locations))
	{
	case EAccessVisualStudioResult::VSInstanceIsOpen:
		{
			// Set Focus on Visual Studio
			CComPtr<EnvDTE::Window> MainWindow;
			if (SUCCEEDED(DTE->get_MainWindow(&MainWindow)) &&
				SUCCEEDED(MainWindow->Activate()))
			{
				bSuccess = true;
			}
			else
			{
				UE_LOG(LogVSAccessor, Warning, TEXT("Couldn't set focus on Visual Studio."));
			}
		}
		break;

	case EAccessVisualStudioResult::VSInstanceIsNotOpen:
		{
			// Automatically fail if there's already an attempt in progress
			if (!IsVSLaunchInProgress())
			{
				bSuccess = RunVisualStudioAndOpenSolution();
			}
		}
		break;

	default:
		// Do nothing if we failed the VS detection, otherwise we could get stuck in a loop of constantly 
		// trying to open a VS instance since we can't detect that one is already running
		break;
	}

	// Uninitialize the com library, if we initialized it above (don't call if S_FALSE)
	FWindowsPlatformMisc::CoUninitialize();

	return bSuccess;
}

bool FVSAccessorModule::OpenVisualStudioFilesInternalViaDTE(const TArray<FileOpenRequest>& Requests, bool& bWasDeferred)
{
	// Initialize the com library, if not already by this thread
	if (!FWindowsPlatformMisc::CoInitialize())
	{
		UE_LOG(LogVSAccessor, Error, TEXT( "ERROR - Could not initialize COM library!" ));
		return false;
	}
	
	bool bDefer = false, bSuccess = false;
	CComPtr<EnvDTE::_DTE> DTE;
	switch (AccessVisualStudioViaDTE(DTE, SolutionPath, Locations))
	{
	case EAccessVisualStudioResult::VSInstanceIsOpen:
		{
			// Set Focus on Visual Studio
			CComPtr<EnvDTE::Window> MainWindow;
			if (SUCCEEDED(DTE->get_MainWindow(&MainWindow)) &&
				SUCCEEDED(MainWindow->Activate()))
			{
				// Get ItemOperations
				CComPtr<EnvDTE::ItemOperations> ItemOperations;
				if (SUCCEEDED(DTE->get_ItemOperations(&ItemOperations)))
				{
					for ( const FileOpenRequest& Request : Requests )
					{
						// Check that the file actually exists first
						if ( !FPaths::FileExists(Request.FullPath) )
						{
							OpenFileFailed.Broadcast(Request.FullPath);
							bSuccess |= false;
							continue;
						}

						// Open File
						auto ANSIPath = StringCast<ANSICHAR>(*Request.FullPath);
						CComBSTR COMStrFileName(ANSIPath.Get());
						CComBSTR COMStrKind(EnvDTE::vsViewKindTextView);
						CComPtr<EnvDTE::Window> Window;
						if ( SUCCEEDED(ItemOperations->OpenFile(COMStrFileName, COMStrKind, &Window)) )
						{
							// If we've made it this far - we've opened the file.  it doesn't matter if
							// we successfully get to the line number.  Everything else is gravy.
							bSuccess |= true;

							// Scroll to Line Number
							CComPtr<EnvDTE::Document> Document;
							CComPtr<IDispatch> SelectionDispatch;
							CComPtr<EnvDTE::TextSelection> Selection;
							if ( SUCCEEDED(DTE->get_ActiveDocument(&Document)) &&
								 SUCCEEDED(Document->get_Selection(&SelectionDispatch)) &&
								 SUCCEEDED(SelectionDispatch->QueryInterface(&Selection)) &&
								 SUCCEEDED(Selection->GotoLine(Request.LineNumber, true)) )
							{
								if ( !SUCCEEDED(Selection->MoveToLineAndOffset(Request.LineNumber, Request.ColumnNumber, false)) )
								{
									UE_LOG(LogVSAccessor, Warning, TEXT("Couldn't goto column number '%i' of line '%i' in '%s'"), Request.ColumnNumber, Request.LineNumber, *Request.FullPath);
								}
							}
							else
							{
								UE_LOG(LogVSAccessor, Warning, TEXT("Couldn't goto line number '%i' in '%s'"), Request.LineNumber, *Request.FullPath);
							}
						}
						else
						{
							UE_LOG(LogVSAccessor, Warning, TEXT("Couldn't open file '%s'."), *Request.FullPath);
						}
					}

					VSLaunchFinished( true );
				}
				else
				{
					UE_LOG(LogVSAccessor, Log, TEXT("Couldn't get item operations. Visual Studio may still be initializing."));
					bDefer = true;
				}
			}
			else
			{
				UE_LOG(LogVSAccessor, Warning, TEXT("Couldn't set focus on Visual Studio."));
			}
		}
		break;

	case EAccessVisualStudioResult::VSInstanceIsNotOpen:
		{
			bDefer = true;

			// We can't process until we're in the main thread, if we aren't initially defer until we are
			if ( IsInGameThread() )
			{
				// If we haven't already attempted to launch VS do so now
				if ( !IsVSLaunchInProgress() )
				{
					// If there's no valid instance of VS running, run one if we have it installed
					if ( !RunVisualStudioAndOpenSolution() )
					{
						bDefer = false;
					}
					else
					{
						VSLaunchStarted();
					}
				}
			}
		}
		break;

	default:
		// Do nothing if we failed the VS detection, otherwise we could get stuck in a loop of constantly 
		// trying to open a VS instance since we can't detect that one is already running
		bDefer = false;
		break;
	}
	
	if ( !bSuccess )
	{
		// If we have attempted to launch VS, and it's taken too long, timeout so the user can try again
		if ( IsVSLaunchInProgress() && ( FPlatformTime::Seconds() - VSLaunchTime ) > 300 )
		{
			// We need todo this in case the process died or was kill prior to the code gaining focus of it
			bDefer = false;
			VSLaunchFinished(false);

			// We failed to open the solution and file, so lets just use the platforms default opener.
			for ( const FileOpenRequest& Request : Requests )
			{
				FPlatformProcess::LaunchFileInDefaultExternalApplication(*Request.FullPath);
			}
		}

		// Defer the request until VS is available to take hold of
		if ( bDefer )
		{
			for ( const FileOpenRequest& Request : Requests )
			{
				const FString DeferCommand = FString::Printf(TEXT("OPEN_VS %s %d %d"), *Request.FullPath, Request.LineNumber, Request.ColumnNumber);
				LaunchVSDeferred.Broadcast(DeferCommand);
			}
		}
		else if ( !bSuccess )
		{
			UE_LOG(LogVSAccessor, Warning, TEXT("Couldn't access Visual Studio"));
		}
	}

	// Uninitialize the com library, if we initialized it above (don't call if S_FALSE)
	FWindowsPlatformMisc::CoUninitialize();

	bWasDeferred = bDefer;
	return bSuccess;
}

bool FVSAccessorModule::SaveAllOpenVisualStudioDocuments()
{
	bool bSuccess = false;

	// Initialize the com library, if not already by this thread
	if (!FWindowsPlatformMisc::CoInitialize())
	{
		UE_LOG(LogVSAccessor, Error, TEXT( "ERROR - Could not initialize COM library!" ));
		return bSuccess;
	}
	
	CComPtr<EnvDTE::_DTE> DTE;
	if (AccessVisualStudioViaDTE(DTE, SolutionPath, Locations) == EAccessVisualStudioResult::VSInstanceIsOpen)
	{
		// Save all documents
		CComPtr<EnvDTE::Documents> Documents;
		if (SUCCEEDED(DTE->get_Documents(&Documents)) &&
			SUCCEEDED(Documents->SaveAll()))
		{
			bSuccess = true;
		}
		else
		{
			UE_LOG(LogVSAccessor, Warning, TEXT("Couldn't save all documents"));
		}
	}
	else
	{
		UE_LOG(LogVSAccessor, Warning, TEXT("Couldn't access Visual Studio"));
	}

	// Uninitialize the com library, if we initialized it above (don't call if S_FALSE)
	FWindowsPlatformMisc::CoUninitialize();

	return bSuccess;
}

bool FVSAccessorModule::Exec( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar )
{
	const TCHAR *Str = Cmd;
	if( FParse::Command(&Str,TEXT("OPEN_VS")) )
	{
		// Valid column number?
		const FString Params( Str );
		const FString Space( TEXT(" ") );
		const int32 ColumnIndex = Params.Find( Space, ESearchCase::CaseSensitive, ESearchDir::FromEnd );
		if ( ColumnIndex != INDEX_NONE )
		{
			// Valid line number?
			const int32 ColumnNumber = FCString::Atoi( *Params.RightChop( ColumnIndex+Space.Len() ) );
			const int32 LineIndex = Params.Find( Space, ESearchCase::CaseSensitive, ESearchDir::FromEnd, ColumnIndex );
			if ( LineIndex != INDEX_NONE )
			{
				// Valid source path?
				const int32 LineNumber = FCString::Atoi( *Params.Mid( LineIndex+Space.Len(), ColumnIndex-LineIndex-Space.Len() ) );
				const FString FullPath = Params.LeftChop( Params.Len() - LineIndex );
				if ( FullPath.Len() > 0 )
				{
					OpenVisualStudioFileAtLineInternal( FullPath, LineNumber, ColumnNumber );
				}
			}
		}
		return true;
	}
	return false;
}

void FVSAccessorModule::VSLaunchStarted()
{
	// Broadcast the info and hope that MainFrame is around to receive it
	LaunchingVS.Broadcast();
	VSLaunchTime = FPlatformTime::Seconds();
}

void FVSAccessorModule::VSLaunchFinished( bool bSuccess )
{
	// Finished all requests! Notify the UI.
	DoneLaunchingVS.Broadcast( bSuccess );
	VSLaunchTime = 0.0;
}

#else

// VS Express-only dummy versions
bool FVSAccessorModule::SaveAllOpenVisualStudioDocuments() { return false; }
bool FVSAccessorModule::Exec( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar ) { return false; }
void FVSAccessorModule::VSLaunchStarted() {}
void FVSAccessorModule::VSLaunchFinished( bool bSuccess ) {}

#endif

bool GetProcessCommandLine(const ::DWORD InProcessID, FString& OutCommandLine)
{
	check(InProcessID);

	// Initialize the com library, if not already by this thread
	if (!FWindowsPlatformMisc::CoInitialize())
	{
		UE_LOG(LogVSAccessor, Error, TEXT("ERROR - Could not initialize COM library!"));
		return false;
	}

	bool bSuccess = false;

	::IWbemLocator *pLoc = nullptr;
	if (SUCCEEDED(::CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc)))
	{
		::IWbemServices *pSvc = nullptr;
		if (SUCCEEDED(pLoc->ConnectServer(BSTR(TEXT("ROOT\\CIMV2")), nullptr, nullptr, nullptr, 0, 0, 0, &pSvc)))
		{
			// Set the proxy so that impersonation of the client occurs
			if (SUCCEEDED(::CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE)))
			{
				::IEnumWbemClassObject* pEnumerator = nullptr;
				const FString WQLQuery = FString::Printf(TEXT("SELECT ProcessId, CommandLine FROM Win32_Process WHERE ProcessId=%lu"), InProcessID);
				if (SUCCEEDED(pSvc->ExecQuery(BSTR(TEXT("WQL")), BSTR(*WQLQuery), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &pEnumerator)))
				{
					while (pEnumerator && !bSuccess)
					{
						::IWbemClassObject *pclsObj = nullptr;
						::ULONG uReturn = 0;
						if (SUCCEEDED(pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn)))
						{
							check(uReturn == 1);

							::VARIANT vtProp;

							::DWORD CurProcessID = 0;
							if (SUCCEEDED(pclsObj->Get(TEXT("ProcessId"), 0, &vtProp, 0, 0)))
							{
								CurProcessID = vtProp.ulVal;
								::VariantClear(&vtProp);
							}

							check(CurProcessID == InProcessID);
							if (SUCCEEDED(pclsObj->Get(TEXT("CommandLine"), 0, &vtProp, 0, 0)))
							{
								OutCommandLine = vtProp.bstrVal;
								::VariantClear(&vtProp);

								bSuccess = true;
							}

							pclsObj->Release();
						}
					}

					pEnumerator->Release();
				}
			}

			pSvc->Release();
		}

		pLoc->Release();
	}

	// Uninitialize the com library, if we initialized it above (don't call if S_FALSE)
	FWindowsPlatformMisc::CoUninitialize();

	return bSuccess;
}

::HWND GetTopWindowForProcess(const ::DWORD InProcessID)
{
	check(InProcessID);

	struct EnumWindowsData
	{
		::DWORD InProcessID;
		::HWND OutHwnd;

		static ::BOOL CALLBACK EnumWindowsProc(::HWND Hwnd, ::LPARAM lParam)
		{
			EnumWindowsData *const Data = (EnumWindowsData*)lParam;

			::DWORD HwndProcessId = 0;
			::GetWindowThreadProcessId(Hwnd, &HwndProcessId);

			if (HwndProcessId == Data->InProcessID)
			{
				Data->OutHwnd = Hwnd;
				return 0; // stop enumeration
			}
			return 1; // continue enumeration
		}
	};

	EnumWindowsData Data;
	Data.InProcessID = InProcessID;
	Data.OutHwnd = nullptr;
	::EnumWindows(&EnumWindowsData::EnumWindowsProc, (LPARAM)&Data);

	return Data.OutHwnd;
}

EAccessVisualStudioResult AccessVisualStudioViaProcess(::DWORD& OutProcessID, FString& OutExecutablePath, const FString& InSolutionPath, const TArray<FVSAccessorModule::VisualStudioLocation>& InLocations)
{
	OutProcessID = 0;

	EAccessVisualStudioResult AccessResult = EAccessVisualStudioResult::VSInstanceIsNotOpen;

	::HANDLE hProcessSnap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hProcessSnap != INVALID_HANDLE_VALUE)
	{
		MODULEENTRY32 ModuleEntry;
		ModuleEntry.dwSize = sizeof(MODULEENTRY32);

		PROCESSENTRY32 ProcEntry;
		ProcEntry.dwSize = sizeof(PROCESSENTRY32);

		// We enumerate the locations as the outer loop to ensure we find our preferred process type first
		// If we did this as the inner loop, then we'd get the first process that matched any location, even if it wasn't our preference
		for (const auto& Location : InLocations)
		{
			for (::BOOL bHasProcess = ::Process32First(hProcessSnap, &ProcEntry); bHasProcess && !OutProcessID; bHasProcess = ::Process32Next(hProcessSnap, &ProcEntry))
			{
				::HANDLE hModuleSnap = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, ProcEntry.th32ProcessID);
				if (hModuleSnap != INVALID_HANDLE_VALUE)
				{
					if (::Module32First(hModuleSnap, &ModuleEntry))
					{
						FString ProcPath = ModuleEntry.szExePath;
						FPaths::NormalizeDirectoryName(ProcPath);
						FPaths::CollapseRelativeDirectories(ProcPath);

						if (ProcPath == Location.ExecutablePath)
						{
							// Without DTE we can't accurately verify that the Visual Studio instance has the correct solution open,
							// however, if we've opened it (or it's opened the solution directly), then the solution path will
							// exist somewhere in the command line for the process
							FString CommandLine;
							if (GetProcessCommandLine(ProcEntry.th32ProcessID, CommandLine))
							{
								FPaths::NormalizeFilename(CommandLine);
								if (CommandLine.Contains(InSolutionPath))
								{
									OutProcessID = ProcEntry.th32ProcessID;
									OutExecutablePath = Location.ExecutablePath;
									AccessResult = EAccessVisualStudioResult::VSInstanceIsOpen;
									break;
								}
							}
							else
							{
								UE_LOG(LogVSAccessor, Warning, TEXT("Couldn't access module information"));
								AccessResult = EAccessVisualStudioResult::VSInstanceUnknown;
							}
						}
					}
					else
					{
						UE_LOG(LogVSAccessor, Warning, TEXT("Couldn't access module table"));
						AccessResult = EAccessVisualStudioResult::VSInstanceUnknown;
					}

					::CloseHandle(hModuleSnap);
				}
				else
				{
					UE_LOG(LogVSAccessor, Warning, TEXT("Couldn't access module table"));
					AccessResult = EAccessVisualStudioResult::VSInstanceUnknown;
				}
			}
		}

		::CloseHandle(hProcessSnap);
	}
	else
	{
		UE_LOG(LogVSAccessor, Warning, TEXT("Couldn't access process table"));
		AccessResult = EAccessVisualStudioResult::VSInstanceUnknown;
	}

	return AccessResult;
}

bool FVSAccessorModule::OpenVisualStudioSolution()
{
#if VSACCESSOR_HAS_DTE
	if(OpenVisualStudioSolutionViaDTE())
	{
		return true;
	}
#endif
	return OpenVisualStudioSolutionViaProcess();
}

bool FVSAccessorModule::OpenVisualStudioFilesInternal(const TArray<FileOpenRequest>& Requests)
{
#if VSACCESSOR_HAS_DTE
	{
		bool bWasDeferred = false;
		if(OpenVisualStudioFilesInternalViaDTE(Requests, bWasDeferred) || bWasDeferred)
		{
			return true;
		}
	}
#endif
	return OpenVisualStudioFilesInternalViaProcess(Requests);
}

bool FVSAccessorModule::OpenVisualStudioSolutionViaProcess()
{
	::DWORD ProcessID = 0;
	FString Path;
	switch (AccessVisualStudioViaProcess(ProcessID, Path, SolutionPath, Locations))
	{
	case EAccessVisualStudioResult::VSInstanceIsOpen:
		{
			// Try to bring Visual Studio to the foreground
			::HWND VisualStudioHwnd = GetTopWindowForProcess(ProcessID);
			if (VisualStudioHwnd)
			{
				// SwitchToThisWindow isn't really intended for general use, however it can switch to 
				// the VS window, where SetForegroundWindow will fail due to process permissions
				::SwitchToThisWindow(VisualStudioHwnd, 0);
			}
			return true;
		}
		break;

	case EAccessVisualStudioResult::VSInstanceIsNotOpen:
		return RunVisualStudioAndOpenSolution();
		
	default:
		// Do nothing if we failed the VS detection, otherwise we could get stuck in a loop of constantly 
		// trying to open a VS instance since we can't detect that one is already running
		break;
	}

	return false;
}

bool FVSAccessorModule::OpenVisualStudioFilesInternalViaProcess(const TArray<FileOpenRequest>& Requests)
{
	::DWORD ProcessID = 0;
	FString Path;
	switch (AccessVisualStudioViaProcess(ProcessID, Path, SolutionPath, Locations))
	{
	case EAccessVisualStudioResult::VSInstanceIsOpen:
		return RunVisualStudioAndOpenSolutionAndFiles(Path, "", &Requests);

	case EAccessVisualStudioResult::VSInstanceIsNotOpen:
		if (CanRunVisualStudio(Path))
		{
			return RunVisualStudioAndOpenSolutionAndFiles(Path, SolutionPath, &Requests);
		}
		break;

	default:
		// Do nothing if we failed the VS detection, otherwise we could get stuck in a loop of constantly 
		// trying to open a VS instance since we can't detect that one is already running
		break;
	}

	return false;
}

bool FVSAccessorModule::CanRunVisualStudio(FString& OutPath) const
{
	// AddVisualStudioVersion only adds paths to Locations if they're valid, so we can just use the first entry in the array
	if (Locations.Num())
	{
		const VisualStudioLocation& NewLocation = Locations[0];
		OutPath = NewLocation.ExecutablePath;
		return true;
	}

	return false;
}

bool FVSAccessorModule::RunVisualStudioAndOpenSolution() const
{
	FString Path;
	if (CanRunVisualStudio(Path))
	{
		return RunVisualStudioAndOpenSolutionAndFiles(Path, SolutionPath, nullptr);
	}

	return false;
}

bool FVSAccessorModule::OpenVisualStudioFiles(const TArray<FString>& FullPaths)
{
	// Automatically fail if there's already an attempt in progress
	if ( !IsVSLaunchInProgress() )
	{
		TArray<FileOpenRequest> Requests;
		for ( const FString& FullPath : FullPaths )
		{
			Requests.Add(FileOpenRequest(FullPath, 0, 0));
		}

		return OpenVisualStudioFilesInternal(Requests);
	}

	return false;
}

bool FVSAccessorModule::OpenVisualStudioFileAtLine(const FString& FullPath, int32 LineNumber, int32 ColumnNumber)
{
	// Automatically fail if there's already an attempt in progress
	if (!IsVSLaunchInProgress())
	{
		return OpenVisualStudioFileAtLineInternal(FullPath, LineNumber, ColumnNumber);
	}

	return false;
}

bool FVSAccessorModule::OpenVisualStudioFileAtLineInternal(const FString& FullPath, int32 LineNumber, int32 ColumnNumber)
{
	TArray<FileOpenRequest> Requests;
	Requests.Add(FileOpenRequest(FullPath, LineNumber, ColumnNumber));

	return OpenVisualStudioFilesInternal(Requests);
}

void FVSAccessorModule::AddVisualStudioVersion(const int MajorVersion, const bool bAllowExpress)
{
	// We can use the common tools macro to work out if this version of Visual Studio is installed
	const FString CommonToolsEnvVar = FString::Printf(TEXT("VS%d0COMNTOOLS"), MajorVersion);
	TCHAR CommonToolsPath[MAX_PATH];
	FPlatformMisc::GetEnvironmentVariable(*CommonToolsEnvVar, CommonToolsPath, ARRAY_COUNT(CommonToolsPath));
	if (!FCString::Strlen(CommonToolsPath))
	{
		return;
	}

	FString BaseExecutablePath = FPaths::Combine(CommonToolsPath, TEXT(".."), TEXT("IDE"));
	FPaths::NormalizeDirectoryName(BaseExecutablePath);
	FPaths::CollapseRelativeDirectories(BaseExecutablePath);

	VisualStudioLocation NewLocation;
	NewLocation.ExecutablePath = BaseExecutablePath / TEXT("devenv.exe");
#if VSACCESSOR_HAS_DTE
	NewLocation.ROTMoniker = FString::Printf(TEXT("!VisualStudio.DTE.%d.0"), MajorVersion);
#endif

	// Only add this version of Visual Studio if the devenv executable actually exists
	const bool bDevEnvExists = FPaths::FileExists(NewLocation.ExecutablePath);
	if (bDevEnvExists)
	{
		Locations.Add(NewLocation);
	}

	if (bAllowExpress)
	{
		NewLocation.ExecutablePath = BaseExecutablePath / TEXT("WDExpress.exe");
#if VSACCESSOR_HAS_DTE
		NewLocation.ROTMoniker = FString::Printf(TEXT("!WDExpress.DTE.%d.0"), MajorVersion);
#endif

		// Only add this version of Visual Studio if the WDExpress executable actually exists
		const bool bWDExpressExists = FPaths::FileExists(NewLocation.ExecutablePath);
		if (bWDExpressExists)
		{
			Locations.Add(NewLocation);
		}
	}
}

bool FVSAccessorModule::RunVisualStudioAndOpenSolutionAndFiles(const FString& ExecutablePath, const FString& SolutionPath, const TArray<FileOpenRequest>* const Requests) const
{
	FString Params;

	// Only open the solution if it exists
	if (!SolutionPath.IsEmpty())
	{
		if (FPaths::FileExists(SolutionPath))
		{
			Params += TEXT("\"");
			Params += SolutionPath;
			Params += TEXT("\"");
		}
		else
		{
			OpenFileFailed.Broadcast(SolutionPath);
			return false;
		}
	}

	if (Requests)
	{
		int32 GoToLine = 0;
		for (const FileOpenRequest& Request : *Requests)
		{
			// Only open the file if it exists
			if (FPaths::FileExists(Request.FullPath))
			{
				Params += TEXT(" \"");
				Params += Request.FullPath;
				Params += TEXT("\"");

				GoToLine = Request.LineNumber;
			}
			else
			{
				OpenFileFailed.Broadcast(Request.FullPath);
				return false;
			}
		}

		if (GoToLine > 0)
		{
			Params += FString::Printf(TEXT(" /command \"edit.goto %d\""), GoToLine);
		}
	}

	bool bSuccess = false;

	if (!Params.IsEmpty())
	{
		FProcHandle WorkerHandle = FPlatformProcess::CreateProc(*ExecutablePath, *Params, true, false, false, nullptr, 0, nullptr, nullptr);
		if (WorkerHandle.IsValid())
		{
			bSuccess = true;
		}
		WorkerHandle.Close();
	}

	return bSuccess;
}
