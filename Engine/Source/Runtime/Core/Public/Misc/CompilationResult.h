// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once


/**
 * Enumerates possible results of a compilation operation.
 *
 * This enum has to be compatible with the one defined in the
 * UE4\Engine\Source\Programs\UnrealBuildTool\System\ExternalExecution.cs file
 * to keep communication between UHT, UBT and Editor compiling processes valid.
 */
namespace ECompilationResult
{
	enum Type
	{
		/** All targets were up to date, used only with -canskiplink */
		UpToDate = -2,
		/** Build was canceled, this is used on the engine side only */
		Canceled = -1,
		/** Compilation succeeded */
		Succeeded = 0,
		/** Compilation failed because generated code changed which was not supported */
		FailedDueToHeaderChange = 1,
		/** Compilation failed due to compilation errors */
		OtherCompilationError = 2,
		/** The process has most likely crashed. This is what UE returns in case of an assert */
		CrashOrAssert = 3,
		/** Compilation is not supported in the current build */
		Unsupported,
		/** Unknown error */
		Unknown
	};

	/**
	* Converts ECompilationResult enum to string.
	*/
	static FORCEINLINE const TCHAR* ToString(ECompilationResult::Type Result)
	{
		switch (Result)
		{
		case ECompilationResult::UpToDate:
			return TEXT("UpToDate");
		case ECompilationResult::Canceled:
			return TEXT("Canceled");
		case ECompilationResult::Succeeded:
			return TEXT("Succeeded");
		case ECompilationResult::FailedDueToHeaderChange:
			return TEXT("FailedDueToHeaderChange");
		case ECompilationResult::OtherCompilationError:
			return TEXT("OtherCompilationError");
		case ECompilationResult::CrashOrAssert:
			return TEXT("CrashOrAssert");
		case ECompilationResult::Unsupported:
			return TEXT("Unsupported");
		};
		return TEXT("Unknown");
	}

	/**
	* Returns false if the provided Result is either UpToDate or Succeeded.
	*/
	static FORCEINLINE bool Failed(ECompilationResult::Type Result)
	{
		return !(Result == ECompilationResult::Succeeded || Result == ECompilationResult::UpToDate);
	}
}
