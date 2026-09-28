// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Slash : ModuleRules
{
	public Slash(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// module root on the include path for cross folder includes
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });

		// ai, navmesh and umg
		PrivateDependencyModuleNames.AddRange(new string[] { "AIModule", "GameplayTasks", "NavigationSystem", "UMG", "Slate", "SlateCore" });
		
		// uncomment for online features

		// OnlineSubsystemSteam needs enabling in the uproject
	}
}
