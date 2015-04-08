// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "MainFramePrivatePCH.h"
#include "CompilerResultsLog.h"
#include "Editor/EditorLiveStreaming/Public/IEditorLiveStreaming.h"
#include "Developer/HotReload/Public/IHotReload.h"
#include "NotificationManager.h"
#include "SNotificationList.h"

DEFINE_LOG_CATEGORY(LogMainFrame);
#define LOCTEXT_NAMESPACE "FMainFrameModule"


const FText StaticGetApplicationTitle( const bool bIncludeGameName )
{
	static const FText ApplicationTitle = NSLOCTEXT("UnrealEditor", "ApplicationTitle", "Unreal Editor");

	if (bIncludeGameName && FApp::HasGameName())
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("GameName"), FText::FromString( FString( FApp::GetGameName())));
		Args.Add(TEXT("AppTitle"), ApplicationTitle);

		const EBuildConfigurations::Type BuildConfig = FApp::GetBuildConfiguration();

		if (BuildConfig != EBuildConfigurations::Shipping && BuildConfig != EBuildConfigurations::Development && BuildConfig != EBuildConfigurations::Unknown)
		{
			Args.Add( TEXT("Config"), EBuildConfigurations::ToText(BuildConfig));

			return FText::Format( NSLOCTEXT("UnrealEditor", "AppTitleGameNameWithConfig", "{GameName} [{Config}] - {AppTitle}"), Args );
		}

		return FText::Format( NSLOCTEXT("UnrealEditor", "AppTitleGameName", "{GameName} - {AppTitle}"), Args );
	}

	return ApplicationTitle;
}


/* IMainFrameModule implementation
 *****************************************************************************/

void FMainFrameModule::CreateDefaultMainFrame( const bool bStartImmersivePIE )
{
	if (!IsWindowInitialized())
	{
		FRootWindowLocation DefaultWindowLocation;

		bool bEmbedTitleAreaContent = true;
		bool bIsUserSizable = true;
		bool bSupportsMaximize = true;
		bool bSupportsMinimize = true;
		FText WindowTitle;
		if ( ShouldShowProjectDialogAtStartup() )
		{
			// Force tabs restored from layout that have no window (the LevelEditor tab) to use a docking area with
			// embedded title area content.  We need to override the behavior here because we're creating the actual
			// window ourselves instead of letting the tab management system create it for us.
			bEmbedTitleAreaContent = false;

			// Do not maximize the window initially. Keep a small dialog feel.
			DefaultWindowLocation.InitiallyMaximized = false;

			DefaultWindowLocation.WindowSize = GetProjectBrowserWindowSize();

			// Do not let the user adjust the fixed size
			bIsUserSizable = true;

			// Since this will appear to be a dialog, there is no need to allow maximizing or minimizing
			bSupportsMaximize = true;
			bSupportsMinimize = true;

			// When opening the project dialog, show "Project Browser" in the window title
			WindowTitle = LOCTEXT("ProjectBrowserDialogTitle", "Unreal Project Browser");
		}
		else
		{
			const bool bIncludeGameName = true;
			WindowTitle = GetApplicationTitle( bIncludeGameName );
		}

		TSharedRef<SWindow> RootWindow = SNew(SWindow)
			.AutoCenter( EAutoCenter::None )
			.Title( WindowTitle )
			.IsInitiallyMaximized( DefaultWindowLocation.InitiallyMaximized )
			.ScreenPosition( DefaultWindowLocation.ScreenPosition )
			.ClientSize( DefaultWindowLocation.WindowSize )
			.CreateTitleBar( !bEmbedTitleAreaContent )
			.SizingRule( bIsUserSizable ? ESizingRule::UserSized : ESizingRule::FixedSize )
			.SupportsMaximize( bSupportsMaximize )
			.SupportsMinimize( bSupportsMinimize );

		const bool bShowRootWindowImmediately = false;
		FSlateApplication::Get().AddWindow( RootWindow, bShowRootWindowImmediately );
		
#if !PLATFORM_MAC // On OS X we don't want Top-Level windows to have a parent, as we don't really support the notion of child windows on that OS
		FGlobalTabmanager::Get()->SetRootWindow(RootWindow);
#endif
		FSlateNotificationManager::Get().SetRootWindow(RootWindow);

		TSharedPtr<SWidget> MainFrameContent;
		bool bLevelEditorIsMainTab = false;
		if ( ShouldShowProjectDialogAtStartup() )
		{
			MainFrameContent = FGameProjectGenerationModule::Get().CreateGameProjectDialog(/*bAllowProjectOpening=*/true, /*bAllowProjectCreate=*/true);
		}
		else
		{
			// Get desktop metrics
			FDisplayMetrics DisplayMetrics;
			FSlateApplication::Get().GetDisplayMetrics( DisplayMetrics );

			// Setup a position and size for the main frame window that's centered in the desktop work area
			const float CenterScale = 0.65f;
			const FVector2D DisplaySize(
				DisplayMetrics.PrimaryDisplayWorkAreaRect.Right - DisplayMetrics.PrimaryDisplayWorkAreaRect.Left,
				DisplayMetrics.PrimaryDisplayWorkAreaRect.Bottom - DisplayMetrics.PrimaryDisplayWorkAreaRect.Top );
			const FVector2D WindowSize = CenterScale * DisplaySize;

			TSharedRef<FTabManager::FLayout> LoadedLayout = FLayoutSaveRestore::LoadFromConfig(GEditorLayoutIni,
				// We persist the positioning of the level editor and the content browser.
				// The asset editors currently do not get saved.
				FTabManager::NewLayout( "UnrealEd_Layout_v1.4" )
				->AddArea
				(
					// level editor window
					FTabManager::NewPrimaryArea()
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(2.0f)
						->AddTab("LevelEditor", ETabState::OpenedTab)
						->AddTab("DockedToolkit", ETabState::ClosedTab)
					)
				)
				->AddArea
				(
					// content browser window
					FTabManager::NewArea(WindowSize)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(1.0f)
						->AddTab("ContentBrowser1Tab", ETabState::ClosedTab)
					)
				)
				->AddArea
				(
					// toolkits window
					FTabManager::NewArea(WindowSize)
					->SetOrientation(Orient_Vertical)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(1.0f)
						->AddTab("StandaloneToolkit", ETabState::ClosedTab)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.35f)
						->AddTab("MergeTool", ETabState::ClosedTab)
					)
				)
				->AddArea
				(
					// settings window
					FTabManager::NewArea(WindowSize)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(1.0f)
						->AddTab("EditorSettings", ETabState::ClosedTab)
						->AddTab("ProjectSettings", ETabState::ClosedTab)
						->AddTab("PluginsEditor", ETabState::ClosedTab)
					)
				)
			);

			MainFrameContent = FGlobalTabmanager::Get()->RestoreFrom( LoadedLayout, RootWindow, bEmbedTitleAreaContent );
			bLevelEditorIsMainTab = true;
		}

		RootWindow->SetContent(MainFrameContent.ToSharedRef());

		TSharedPtr<SDockTab> MainTab;
		if ( bLevelEditorIsMainTab )
		{
			MainTab = FGlobalTabmanager::Get()->InvokeTab( FTabId("LevelEditor") );

			// make sure we only allow the message log to be shown when we have a level editor main tab
			FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
			MessageLogModule.EnableMessageLogDisplay(true);
		}

		// Initialize the main frame window
		MainFrameHandler->OnMainFrameGenerated( MainTab, RootWindow );
		
		// Show the window!
		MainFrameHandler->ShowMainFrameWindow( RootWindow, bStartImmersivePIE );
		
		MRUFavoritesList = new FMainMRUFavoritesList;
		MRUFavoritesList->ReadFromINI();

		MainFrameCreationFinishedEvent.Broadcast(RootWindow, ShouldShowProjectDialogAtStartup());
	}
}


TSharedRef<SWidget> FMainFrameModule::MakeMainMenu( const TSharedPtr<FTabManager>& TabManager, const TSharedRef< FExtender > Extender ) const
{
	return FMainMenu::MakeMainMenu( TabManager, Extender );
}


TSharedRef<SWidget> FMainFrameModule::MakeMainTabMenu( const TSharedPtr<FTabManager>& TabManager, const TSharedRef< FExtender > Extender ) const
{
	return FMainMenu::MakeMainTabMenu( TabManager, Extender );
}


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> FMainFrameModule::MakeDeveloperTools() const
{
	struct Local
	{
		static FText GetFrameRateAsString() 
		{
			// Clamp to avoid huge averages at startup or after hitches
			const float AverageFPS = 1.0f / FSlateApplication::Get().GetAverageDeltaTime();
			const float ClampedFPS = ( AverageFPS < 0.0f || AverageFPS > 4000.0f ) ? 0.0f : AverageFPS;

			static const FNumberFormattingOptions FormatOptions = FNumberFormattingOptions()
				.SetMinimumFractionalDigits(1)
				.SetMaximumFractionalDigits(1);
			return FText::AsNumber( ClampedFPS, &FormatOptions );
		}

		static FText GetFrameTimeAsString() 
		{
			// Clamp to avoid huge averages at startup or after hitches
			const float AverageMS = FSlateApplication::Get().GetAverageDeltaTime() * 1000.0f;
			const float ClampedMS = ( AverageMS < 0.0f || AverageMS > 4000.0f ) ? 0.0f : AverageMS;

			static const FNumberFormattingOptions FormatOptions = FNumberFormattingOptions()
				.SetMinimumFractionalDigits(1)
				.SetMaximumFractionalDigits(1);
			static const FText FrameTimeFmt = FText::FromString(TEXT("{0} ms"));
			return FText::Format( FrameTimeFmt, FText::AsNumber( ClampedMS, &FormatOptions ) );
		}

		static FText GetMemoryAsString() 
		{
			// Only refresh process memory allocated after every so often, to reduce fixed frame time overhead
			static SIZE_T StaticLastTotalAllocated = 0;
			static int32 QueriesUntilUpdate = 1;
			if( --QueriesUntilUpdate <= 0 )
			{
				// Query OS for process memory used
				FPlatformMemoryStats MemoryStats = FPlatformMemory::GetStats();
				StaticLastTotalAllocated = MemoryStats.UsedPhysical;

				// Wait 60 queries until we refresh memory again
				QueriesUntilUpdate = 60;
			}

			static const FNumberFormattingOptions FormatOptions = FNumberFormattingOptions()
				.SetMinimumFractionalDigits(2)
				.SetMaximumFractionalDigits(2);
			static const FText MemorySizeFmt = FText::FromString(TEXT("{0} mb"));
			return FText::Format( MemorySizeFmt, FText::AsNumber( (float)StaticLastTotalAllocated / ( 1024.0f * 1024.0f ), &FormatOptions ) );
		}

		static FText GetUObjectCountAsString() 
		{
			return FText::AsNumber(GetUObjectArray().GetObjectArrayNumMinusAvailable());
		}

		static void OpenVideo( FString SourceFilePath )
		{
			FPlatformProcess::ExploreFolder( *( FPaths::GetPath(SourceFilePath) ) );
		}

		/** Clicked the SaveVideo button available */
		static FReply OnClickSaveVideo()
		{
			// Default the result to fail it will be set to  SNotificationItem::CS_Success if saved ok
			SNotificationItem::ECompletionState SaveResultState = SNotificationItem::CS_Fail;
			// The string we will use to tell the user the result of the save
			FText VideoSaveResultText;
			FString HyperLinkText;

			// Capture unavailable or inactive error string
			ICrashTrackerModule* CrashTracker = FModuleManager::LoadModulePtr<ICrashTrackerModule>("CrashTracker");
			if(CrashTracker)
			{
				FString VideoSaveName;
				EWriteUserCaptureVideoError::Type WriteResult = CrashTracker->WriteUserVideoNow( VideoSaveName );
				// If this returns None the capture was successful, otherwise report the error
				if( WriteResult == EWriteUserCaptureVideoError::None )
				{
					// Setup the string with the path and name of the file
					VideoSaveResultText = LOCTEXT( "VideoSavedAs", "Video capture saved as" );					
					HyperLinkText = FPaths::ConvertRelativePathToFull(VideoSaveName);	
					// Flag success
					SaveResultState = SNotificationItem::CS_Success;
				}
				else
				{
					// Write returned an error - differentiate between directory creation failure and the capture unavailable
					if( WriteResult == EWriteUserCaptureVideoError::FailedToCreateDirectory )
					{
						FFormatNamedArguments Args;
						Args.Add( TEXT("VideoCaptureDirectory"), FText::FromString( FPaths::ConvertRelativePathToFull(FPaths::VideoCaptureDir()) ) );
						VideoSaveResultText = LOCTEXT( "VideoSavedFailedFailedToCreateDir", "Video capture save failed - Failed to create directory\n{VideoCaptureDirectory}" );
					}
					else
					{
						VideoSaveResultText = LOCTEXT( "VideoSavedFailedNotRunning", "Video capture save failed - Capture not active or unavailable" );
					}
				}
			}
			else
			{
				// This shouldn't happen as the button is hidden when there is no crash tracker
				VideoSaveResultText = LOCTEXT( "VideoSavedFailedNoTracker", "Video capture failed - CrashTracker inactive" );				
			}

			// Inform the user of the result of the operation
			FNotificationInfo Info( VideoSaveResultText );
			Info.ExpireDuration = 5.0f;
			Info.FadeOutDuration = 0.5f;
			Info.bUseSuccessFailIcons = false;
			Info.bUseLargeFont = false;
			if( HyperLinkText != "" )
			{
				Info.Hyperlink = FSimpleDelegate::CreateStatic(&Local::OpenVideo, HyperLinkText );
				Info.HyperlinkText = FText::FromString( HyperLinkText );
			}
			
			TWeakPtr<SNotificationItem> SaveMessagePtr;
			SaveMessagePtr = FSlateNotificationManager::Get().AddNotification(Info);
			SaveMessagePtr.Pin()->SetCompletionState(SaveResultState);

			return FReply::Handled();
		}


		/** @return Returns true if frame rate and memory should be displayed in the UI */
		static EVisibility ShouldShowFrameRateAndMemory()
		{
			return GEditor->GetEditorUserSettings().bShowFrameRateAndMemory ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
		}
	};


	
	const FSuperSearchModule& SuperSearchModule = FModuleManager::LoadModuleChecked< FSuperSearchModule >(TEXT("SuperSearch"));
	
	// We need the output log module in order to instantiate SConsoleInputBox widgets
	const FOutputLogModule& OutputLogModule = FModuleManager::LoadModuleChecked< FOutputLogModule >(TEXT("OutputLog"));

	const FSlateFontInfo& SmallFixedFont = FEditorStyle::GetFontStyle(TEXT("MainFrame.DebugTools.SmallFont") );
	const FSlateFontInfo& NormalFixedFont = FEditorStyle::GetFontStyle(TEXT("MainFrame.DebugTools.NormalFont") );
	const FSlateFontInfo& LabelFont = FEditorStyle::GetFontStyle(TEXT("MainFrame.DebugTools.LabelFont") );

	TSharedPtr< SWidget > DeveloperTools;
	TSharedPtr< SEditableTextBox > ExposedEditableTextBox;

	ICrashTrackerModule* CrashTracker = FModuleManager::LoadModulePtr<ICrashTrackerModule>("CrashTracker");
	bool bCrashTrackerVideoAvailable = false;
	if (CrashTracker)
	{
		bCrashTrackerVideoAvailable = CrashTracker->IsVideoCaptureAvailable();
	}

	TSharedRef<SWidget> FrameRateAndMemoryWidget =
		SNew( SHorizontalBox )
		.Visibility_Static( &Local::ShouldShowFrameRateAndMemory )

		// FPS
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding( 0.0f, 0.0f, 4.0f, 0.0f )
		[
			SNew( SHorizontalBox )
			.Visibility( GIsDemoMode ? EVisibility::Collapsed : EVisibility::HitTestInvisible )
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Bottom)
			[
				SNew( STextBlock )
				.Text( LOCTEXT("FrameRateLabel", "FPS: ") )
				.Font( LabelFont )
				.ColorAndOpacity( FLinearColor( 0.3f, 0.3f, 0.3f ) )
			]
			+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Bottom)
				[
					SNew( STextBlock )
					.Text_Static( &Local::GetFrameRateAsString )
					.Font(NormalFixedFont)
					.ColorAndOpacity( FLinearColor( 0.6f, 0.6f, 0.6f ) )
				]
			+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Bottom)
				.Padding( 4.0f, 0.0f, 0.0f, 0.0f )
				[
					SNew( STextBlock )
					.Text( LOCTEXT("FrameRate/FrameTime", "/") )
					.Font( SmallFixedFont )
					.ColorAndOpacity( FLinearColor( 0.4f, 0.4f, 0.4f ) )
				]
			+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Bottom)
				.Padding( 4.0f, 0.0f, 0.0f, 0.0f )
				[
					SNew( STextBlock )
					.Text_Static( &Local::GetFrameTimeAsString )
					.Font( SmallFixedFont )
					.ColorAndOpacity( FLinearColor( 0.4f, 0.4f, 0.4f ) )
				]
		]

		// Memory
		+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding( 4.0f, 0.0f, 4.0f, 0.0f )
			[
				SNew( SHorizontalBox )
				.Visibility( GIsDemoMode ? EVisibility::Collapsed : EVisibility::HitTestInvisible )
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Bottom)
				[
					SNew( STextBlock )
					.Text( LOCTEXT("MemoryLabel", "Mem: ") )
					.Font( LabelFont )
					.ColorAndOpacity( FLinearColor( 0.3f, 0.3f, 0.3f ) )
				]
				+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Bottom)
					[
						SNew( STextBlock )
						.Text_Static( &Local::GetMemoryAsString )
						.Font(NormalFixedFont)
						.ColorAndOpacity( FLinearColor( 0.6f, 0.6f, 0.6f ) )
					]
			]

		// UObject count
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding( 4.0f, 0.0f, 4.0f, 0.0f )
		[
			SNew( SHorizontalBox )
				.Visibility( GIsDemoMode ? EVisibility::Collapsed : EVisibility::HitTestInvisible )
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Bottom)
				[
					SNew( STextBlock )
						.Text( LOCTEXT("UObjectCountLabel", "Objs: ") )
						.Font( LabelFont )
						.ColorAndOpacity( FLinearColor( 0.3f, 0.3f, 0.3f ) )
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Bottom)
				[
					SNew( STextBlock )
						.Text_Static( &Local::GetUObjectCountAsString )
						.Font(NormalFixedFont)
						.ColorAndOpacity( FLinearColor( 0.6f, 0.6f, 0.6f ) )
				]
		]
	;

	bool bUseSuperSearch = true;

	// Invisible border, so that we can animate our box panel size
	return SNew( SBorder )
		.Visibility( EVisibility::SelfHitTestInvisible )
		.Padding( FMargin(0.0f, 0.0f, 0.0f, 1.0f) )
		.VAlign(VAlign_Bottom)
		.BorderImage( FEditorStyle::GetBrush("NoBorder") )
		[
			SNew( SHorizontalBox )
			.Visibility( EVisibility::SelfHitTestInvisible )

			+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding( 0.0f )
				[
					FrameRateAndMemoryWidget
				]

			+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Bottom)
				.Padding( 0.0f )
				[
					SNew(SBox)
					.Padding( FMargin( 4.0f, 0.0f, 0.0f, 0.0f ) )
					[
						bUseSuperSearch ? SuperSearchModule.MakeSearchBox( ExposedEditableTextBox ) : OutputLogModule.MakeConsoleInputBox( ExposedEditableTextBox )
					]
				]
/*
			+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding( 6.0f, 0.0f, 2.0f, 0.0f )
				[
					SNew(SComboButton)
						.ButtonContent()
						[
							SNew(SImage)
								.Image(FEditorStyle::GetBrush("Icons.Search"))
						]
						.ContentPadding(FMargin(0.0f, 0.0f, 2.0f, 0.0f))
						.MenuContent()
						[
							SNew(SBox)
								.Padding(8.0f)
								.WidthOverride(512.0f)
								[
									ISearchUIModule::Get().CreateSearchPanel()
								]
						]
						.ToolTipText(LOCTEXT("SearchEditor", "Search the Editor"))
				]
*/
			// Editor live streaming toggle button
			+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign( VAlign_Bottom )
				[
					SNew(SButton)
					.Visibility_Static( []() -> EVisibility { return IEditorLiveStreaming::Get().IsLiveStreamingAvailable() ? EVisibility::Visible : EVisibility::Collapsed; } )
					.ToolTipText( LOCTEXT( "BroadcastTooltip", "Starts or stops broadcasting of this editor session to a live internet streaming service." ) )
					.OnClicked_Static( []
						{ 
							// Toggle broadcasting on or off
							if( IEditorLiveStreaming::Get().IsBroadcastingEditor() ) 
							{
								IEditorLiveStreaming::Get().StopBroadcastingEditor();
							}
							else
							{
								IEditorLiveStreaming::Get().StartBroadcastingEditor();
							}
							return FReply::Handled();
						} )
					.ButtonStyle( FEditorStyle::Get(), "NoBorder" )
					.ContentPadding(FMargin(1,0))
					[
						SNew(SImage)
						.Image( FEditorStyle::GetBrush("EditorLiveStreaming.BroadcastButton") )
						.ColorAndOpacity_Static( [] 
							{ 
								// Pulsate the button graphics while we're broadcasting
								FSlateColor Color( FLinearColor::White );
								if( IEditorLiveStreaming::Get().IsBroadcastingEditor() )
								{
									Color = FLinearColor( 1.0f, 1.0f, 1.0f, FMath::MakePulsatingValue( FSlateApplication::Get().GetCurrentTime(), 2.0f ) );
								}
								return Color;
							} )
					]
				]

			// Crash report "save video" button
			+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign( VAlign_Bottom )
				[
					SNew(SButton)
					.Visibility( bCrashTrackerVideoAvailable ? EVisibility::Visible : EVisibility::Collapsed )						
					.ToolTipText( LOCTEXT( "SaveReplayTooltip", "Saves a video of the last 20 seconds of your work." ) )
					.OnClicked_Static( &Local::OnClickSaveVideo )
					.ButtonStyle( FEditorStyle::Get(), "NoBorder" )
					.ContentPadding(FMargin(1,0))
					[
						SNew(SImage)
						. Image( FEditorStyle::GetBrush("CrashTracker.Record") )
					]
				]
		];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION


void FMainFrameModule::SetLevelNameForWindowTitle( const FString& InLevelFileName )
{
	LoadedLevelName = (InLevelFileName.Len() > 0)
		? FPaths::GetBaseFilename(InLevelFileName)
		: NSLOCTEXT("UnrealEd", "Untitled", "Untitled" ).ToString();
}


/* IModuleInterface implementation
 *****************************************************************************/

void FMainFrameModule::StartupModule( )
{
	MRUFavoritesList = NULL;

	MainFrameHandler = MakeShareable(new FMainFrameHandler);

	FMainFrameCommands::Register();

	SetLevelNameForWindowTitle(TEXT(""));

	// Register to find out about when hot reload completes, so we can show a notification
	IHotReloadModule& HotReloadModule = IHotReloadModule::Get();
	HotReloadModule.OnModuleCompilerStarted().AddRaw( this, &FMainFrameModule::HandleLevelEditorModuleCompileStarted );
	HotReloadModule.OnModuleCompilerFinished().AddRaw( this, &FMainFrameModule::HandleLevelEditorModuleCompileFinished );
	HotReloadModule.OnHotReload().AddRaw( this, &FMainFrameModule::HandleHotReloadFinished );

#if WITH_EDITOR
	ISourceCodeAccessModule& SourceCodeAccessModule = FModuleManager::LoadModuleChecked<ISourceCodeAccessModule>("SourceCodeAccess");
	SourceCodeAccessModule.OnLaunchingCodeAccessor().AddRaw( this, &FMainFrameModule::HandleCodeAccessorLaunching );
	SourceCodeAccessModule.OnDoneLaunchingCodeAccessor().AddRaw( this, &FMainFrameModule::HandleCodeAccessorLaunched );
	SourceCodeAccessModule.OnOpenFileFailed().AddRaw( this, &FMainFrameModule::HandleCodeAccessorOpenFileFailed );
#endif

	// load sounds
	CompileStartSound = LoadObject<USoundBase>(NULL, TEXT("/Engine/EditorSounds/Notifications/CompileStart_Cue.CompileStart_Cue"));
	CompileStartSound->AddToRoot();
	CompileSuccessSound = LoadObject<USoundBase>(NULL, TEXT("/Engine/EditorSounds/Notifications/CompileSuccess_Cue.CompileSuccess_Cue"));
	CompileSuccessSound->AddToRoot();
	CompileFailSound = LoadObject<USoundBase>(NULL, TEXT("/Engine/EditorSounds/Notifications/CompileFailed_Cue.CompileFailed_Cue"));
	CompileFailSound->AddToRoot();

	ModuleCompileStartTime = 0.0f;

	// migrate old layout settings
	FLayoutSaveRestore::MigrateConfig(GEditorUserSettingsIni, GEditorLayoutIni);
}


void FMainFrameModule::ShutdownModule( )
{
	// Destroy the main frame window
	TSharedPtr< SWindow > ParentWindow( GetParentWindow() );
	if( ParentWindow.IsValid() )
	{
		ParentWindow->DestroyWindowImmediately();
	}

	MainFrameHandler.Reset();

	FMainFrameCommands::Unregister();

	if( IHotReloadModule::IsAvailable() )
	{
		IHotReloadModule& HotReloadModule = IHotReloadModule::Get();
		HotReloadModule.OnHotReload().RemoveAll( this );
		HotReloadModule.OnModuleCompilerStarted().RemoveAll( this );
		HotReloadModule.OnModuleCompilerFinished().RemoveAll( this );
	}

#if WITH_EDITOR
	if(FModuleManager::Get().IsModuleLoaded("SourceCodeAccess"))
	{
		ISourceCodeAccessModule& SourceCodeAccessModule = FModuleManager::GetModuleChecked<ISourceCodeAccessModule>("SourceCodeAccess");
		SourceCodeAccessModule.OnLaunchingCodeAccessor().RemoveAll( this );
		SourceCodeAccessModule.OnDoneLaunchingCodeAccessor().RemoveAll( this );
		SourceCodeAccessModule.OnOpenFileFailed().RemoveAll( this );
	}
#endif

	if(CompileStartSound != NULL)
	{
		if (!GExitPurge)
		{
			CompileStartSound->RemoveFromRoot();
		}
		CompileStartSound = NULL;
	}

	if(CompileSuccessSound != NULL)
	{
		if (!GExitPurge)
		{
			CompileSuccessSound->RemoveFromRoot();
		}
		CompileSuccessSound = NULL;
	}

	if(CompileFailSound != NULL)
	{
		if (!GExitPurge)
		{
			CompileFailSound->RemoveFromRoot();
		}
		CompileFailSound = NULL;
	}
}


/* FMainFrameModule implementation
 *****************************************************************************/

bool FMainFrameModule::ShouldShowProjectDialogAtStartup( ) const
{
	return !FApp::HasGameName();
}


/* FMainFrameModule event handlers
 *****************************************************************************/

void FMainFrameModule::HandleLevelEditorModuleCompileStarted( bool bIsAsyncCompile )
{
	ModuleCompileStartTime = FPlatformTime::Seconds();

	if (CompileNotificationPtr.IsValid())
	{
		CompileNotificationPtr.Pin()->ExpireAndFadeout();
	}

	if ( GEditor )
	{
		GEditor->PlayPreviewSound(CompileStartSound);
	}

	FNotificationInfo Info( NSLOCTEXT("MainFrame", "RecompileInProgress", "Compiling C++ Code") );
	Info.Image = FEditorStyle::GetBrush(TEXT("LevelEditor.RecompileGameCode"));
	Info.ExpireDuration = 5.0f;
	Info.bFireAndForget = false;
	
	// We can only show the cancel button on async builds
	if (bIsAsyncCompile)
	{
		Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("CancelC++Compilation", "Cancel"), FText(), FSimpleDelegate::CreateRaw(this, &FMainFrameModule::OnCancelCodeCompilationClicked)));
	}

	CompileNotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);

	if (CompileNotificationPtr.IsValid())
	{
		CompileNotificationPtr.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

void FMainFrameModule::OnCancelCodeCompilationClicked()
{
	IHotReloadModule::Get().RequestStopCompilation();
}

void FMainFrameModule::HandleLevelEditorModuleCompileFinished(const FString& LogDump, ECompilationResult::Type CompilationResult, bool bShowLog)
{
	// Track stats
	{
		const float ModuleCompileDuration = (float)(FPlatformTime::Seconds() - ModuleCompileStartTime);
		UE_LOG(LogMainFrame, Log, TEXT("MainFrame: Module compiling took %.3f seconds"), ModuleCompileDuration);

		if( FEngineAnalytics::IsAvailable() )
		{
			TArray< FAnalyticsEventAttribute > CompileAttribs;
			CompileAttribs.Add(FAnalyticsEventAttribute(TEXT("Duration"), FString::Printf(TEXT("%.3f"), ModuleCompileDuration)));
			CompileAttribs.Add(FAnalyticsEventAttribute(TEXT("Result"), ECompilationResult::ToString(CompilationResult)));
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Modules.Recompile"), CompileAttribs);
		}
	}

	TSharedPtr<SNotificationItem> NotificationItem = CompileNotificationPtr.Pin();

	if (NotificationItem.IsValid())
	{
		if (!ECompilationResult::Failed(CompilationResult))
		{
			if ( GEditor )
			{
				GEditor->PlayPreviewSound(CompileSuccessSound);
			}

			NotificationItem->SetText(NSLOCTEXT("MainFrame", "RecompileComplete", "Compile Complete!"));
			NotificationItem->SetExpireDuration( 5.0f );
			NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
		}
		else
		{
			struct Local
			{
				static void ShowCompileLog()
				{
					FMessageLogModule& MessageLogModule = FModuleManager::GetModuleChecked<FMessageLogModule>("MessageLog");
					MessageLogModule.OpenMessageLog(FCompilerResultsLog::GetLogName());
				}
			};

			if ( GEditor )
			{
				GEditor->PlayPreviewSound(CompileFailSound);
			}

			if (CompilationResult == ECompilationResult::FailedDueToHeaderChange)
			{
				NotificationItem->SetText(NSLOCTEXT("MainFrame", "RecompileFailedDueToHeaderChange", "Compile failed due to the header changes. Close the editor and recompile project in IDE to apply changes."));
			}
			else if (CompilationResult == ECompilationResult::Canceled)
			{
				NotificationItem->SetText(NSLOCTEXT("MainFrame", "RecompileFailed", "Compile Canceled!"));
			}
			else
			{
				NotificationItem->SetText(NSLOCTEXT("MainFrame", "RecompileFailed", "Compile Failed!"));
			}
			
			NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
			NotificationItem->SetHyperlink(FSimpleDelegate::CreateStatic(&Local::ShowCompileLog));
			NotificationItem->SetExpireDuration(30.0f);
		}

		NotificationItem->ExpireAndFadeout();

		CompileNotificationPtr.Reset();
	}
}


void FMainFrameModule::HandleHotReloadFinished( bool bWasTriggeredAutomatically )
{
	// Only play the notification for hot reloads that were triggered automatically.  If the user triggered the hot reload, they'll
	// have a different visual cue for that, such as the "Compiling Complete!" notification
	if( bWasTriggeredAutomatically )
	{
		FNotificationInfo Info( LOCTEXT("HotReloadFinished", "Hot Reload Complete!") );
		Info.Image = FEditorStyle::GetBrush(TEXT("LevelEditor.RecompileGameCode"));
		Info.FadeInDuration = 0.1f;
		Info.FadeOutDuration = 0.5f;
		Info.ExpireDuration = 1.5f;
		Info.bUseThrobber = false;
		Info.bUseSuccessFailIcons = true;
		Info.bUseLargeFont = true;
		Info.bFireAndForget = false;
		Info.bAllowThrottleWhenFrameRateIsLow = false;
		auto NotificationItem = FSlateNotificationManager::Get().AddNotification( Info );
		NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
		NotificationItem->ExpireAndFadeout();
	
		GEditor->PlayPreviewSound(CompileSuccessSound);
	}
}


void FMainFrameModule::HandleCodeAccessorLaunched( const bool WasSuccessful )
{
	TSharedPtr<SNotificationItem> NotificationItem = CodeAccessorNotificationPtr.Pin();
	
	if (NotificationItem.IsValid())
	{
		ISourceCodeAccessModule& SourceCodeAccessModule = FModuleManager::LoadModuleChecked<ISourceCodeAccessModule>("SourceCodeAccess");
		const FText AccessorNameText = SourceCodeAccessModule.GetAccessor().GetNameText();

		if (WasSuccessful)
		{
			NotificationItem->SetText( FText::Format(LOCTEXT("CodeAccessorLoadComplete", "{0} loaded!"), AccessorNameText) );
			NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
		}
		else
		{
			NotificationItem->SetText( FText::Format(LOCTEXT("CodeAccessorLoadFailed", "{0} failed to launch!"), AccessorNameText) );
			NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
		}

		NotificationItem->ExpireAndFadeout();
		CodeAccessorNotificationPtr.Reset();
	}
}


void FMainFrameModule::HandleCodeAccessorLaunching( )
{
	if (CodeAccessorNotificationPtr.IsValid())
	{
		CodeAccessorNotificationPtr.Pin()->ExpireAndFadeout();
	}

	ISourceCodeAccessModule& SourceCodeAccessModule = FModuleManager::LoadModuleChecked<ISourceCodeAccessModule>("SourceCodeAccess");
	const FText AccessorNameText = SourceCodeAccessModule.GetAccessor().GetNameText();

	FNotificationInfo Info( FText::Format(LOCTEXT("CodeAccessorLoadInProgress", "Loading {0}"), AccessorNameText) );
	Info.bFireAndForget = false;

	CodeAccessorNotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
	CodeAccessorNotificationPtr.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
}

void FMainFrameModule::HandleCodeAccessorOpenFileFailed(const FString& Filename)
{
	auto* Info = new FNotificationInfo(FText::Format(LOCTEXT("FileNotFound", "Could not find code file ({Filename})"), FText::FromString(Filename)));
	Info->ExpireDuration = 3.0f;
	FSlateNotificationManager::Get().QueueNotification(Info);
}

#undef LOCTEXT_NAMESPACE


IMPLEMENT_MODULE(FMainFrameModule, MainFrame);
