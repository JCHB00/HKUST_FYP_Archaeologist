// Editor-only analysis commandlet: dumps Blueprint graphs and level actor layout to plain text.

#include "ArchaeoDumpCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "K2Node_CallFunction.h"
#include "Components/StaticMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogArchaeoDump, Log, All);

UArchaeoDumpCommandlet::UArchaeoDumpCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

FString UArchaeoDumpCommandlet::PinTypeToString(const FEdGraphPinType& PinType)
{
	FString Result = PinType.PinCategory.ToString();
	if (PinType.PinSubCategoryObject != nullptr)
	{
		Result += TEXT(":") + PinType.PinSubCategoryObject->GetName();
	}
	switch (PinType.ContainerType)
	{
	case EPinContainerType::Array:   Result += TEXT("[]"); break;
	case EPinContainerType::Set:     Result += TEXT("(set)"); break;
	case EPinContainerType::Map:     Result += TEXT("(map)"); break;
	default: break;
	}
	return Result;
}

FString UArchaeoDumpCommandlet::GraphToText(UEdGraph* Graph)
{
	FString S;
	if (Graph == nullptr)
	{
		return S;
	}

	S += FString::Printf(TEXT("\n===== GRAPH: %s  (%s)  nodes=%d =====\n"),
		*Graph->GetName(), *Graph->GetClass()->GetName(), Graph->Nodes.Num());

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node == nullptr)
		{
			continue;
		}

		FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		Title.ReplaceInline(TEXT("\n"), TEXT(" / "));

		S += FString::Printf(TEXT("\n[%s]  \"%s\"  class=%s  pos=(%d,%d)\n"),
			*Node->GetName(), *Title, *Node->GetClass()->GetName(), Node->NodePosX, Node->NodePosY);

		if (!Node->NodeComment.IsEmpty())
		{
			S += FString::Printf(TEXT("    COMMENT: %s\n"), *Node->NodeComment);
		}

		if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
		{
			const UClass* ParentClass = CallNode->FunctionReference.GetMemberParentClass();
			S += FString::Printf(TEXT("    FUNCTION: %s::%s\n"),
				ParentClass ? *ParentClass->GetName() : TEXT("?"),
				*CallNode->GetFunctionName().ToString());
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin == nullptr)
			{
				continue;
			}

			const TCHAR* Dir = (Pin->Direction == EGPD_Input) ? TEXT("IN ") : TEXT("OUT");

			FString PinLabel = Pin->PinName.ToString();
			if (!Pin->PinFriendlyName.IsEmpty())
			{
				PinLabel += FString::Printf(TEXT("|%s"), *Pin->PinFriendlyName.ToString());
			}

			FString Value;
			if (!Pin->DefaultValue.IsEmpty())
			{
				Value += TEXT("  =") + Pin->DefaultValue;
			}
			if (Pin->DefaultObject != nullptr)
			{
				Value += FString::Printf(TEXT("  =obj:%s"), *Pin->DefaultObject->GetName());
			}
			if (Pin->bHidden && Pin->LinkedTo.Num() == 0 && Value.IsEmpty())
			{
				continue;
			}

			FString Links;
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin == nullptr)
				{
					continue;
				}
				UEdGraphNode* LinkedNode = LinkedPin->GetOwningNodeUnchecked();
				Links += FString::Printf(TEXT("  -> %s.%s"),
					LinkedNode ? *LinkedNode->GetName() : TEXT("<null>"),
					*LinkedPin->PinName.ToString());
			}

			S += FString::Printf(TEXT("    %s %-28s (%-22s)%s%s\n"),
				Dir, *PinLabel, *PinTypeToString(Pin->PinType), *Value, *Links);
		}
	}

	return S;
}

void UArchaeoDumpCommandlet::DumpBlueprintAsset(const FAssetData& Asset, const FString& OutDir)
{
	UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
	if (Blueprint == nullptr)
	{
		UE_LOG(LogArchaeoDump, Warning, TEXT("Could not load blueprint %s"), *Asset.GetObjectPathString());
		return;
	}

	FString S;
	S += FString::Printf(TEXT("ASSET: %s\n"), *Asset.GetObjectPathString());
	S += FString::Printf(TEXT("BLUEPRINT CLASS: %s\n"), *Blueprint->GetClass()->GetName());
	S += FString::Printf(TEXT("PARENT: %s\n"), Blueprint->ParentClass ? *Blueprint->ParentClass->GetName() : TEXT("<none>"));

	FString Interfaces;
	for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
	{
		if (InterfaceDesc.Interface != nullptr)
		{
			Interfaces += InterfaceDesc.Interface->GetName() + TEXT(" ");
		}
	}
	S += FString::Printf(TEXT("INTERFACES: %s\n"), Interfaces.IsEmpty() ? TEXT("<none>") : *Interfaces);

	S += TEXT("\n----- VARIABLES -----\n");
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		S += FString::Printf(TEXT("  %-32s type=%-28s default=\"%s\"  cat=%s  flags=%d\n"),
			*Var.VarName.ToString(),
			*PinTypeToString(Var.VarType),
			*Var.DefaultValue,
			*Var.Category.ToString(),
			(int32)Var.PropertyFlags);
	}

	S += TEXT("\n----- COMPONENTS (SCS) -----\n");
	if (Blueprint->SimpleConstructionScript != nullptr)
	{
		for (USCS_Node* SCSNode : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (SCSNode == nullptr)
			{
				continue;
			}
			const FString ParentName = SCSNode->ParentComponentOrVariableName.ToString();
			S += FString::Printf(TEXT("  %-28s class=%-40s parent=%s\n"),
				*SCSNode->GetVariableName().ToString(),
				SCSNode->ComponentClass ? *SCSNode->ComponentClass->GetName() : TEXT("<none>"),
				ParentName.IsEmpty() ? TEXT("<root>") : *ParentName);
		}
	}

	const TCHAR* GraphListNames[] = { TEXT("UBERGRAPH"), TEXT("FUNCTION"), TEXT("MACRO"), TEXT("DELEGATE") };
	TArray<TArray<TObjectPtr<UEdGraph>>> GraphLists;
	GraphLists.Add(Blueprint->UbergraphPages);
	GraphLists.Add(Blueprint->FunctionGraphs);
	GraphLists.Add(Blueprint->MacroGraphs);
	GraphLists.Add(Blueprint->DelegateSignatureGraphs);

	for (int32 ListIndex = 0; ListIndex < GraphLists.Num(); ++ListIndex)
	{
		for (TObjectPtr<UEdGraph>& GraphPtr : GraphLists[ListIndex])
		{
			if (GraphPtr == nullptr)
			{
				continue;
			}
			S += FString::Printf(TEXT("\n########## %s GRAPH ##########"), GraphListNames[ListIndex]);
			S += GraphToText(GraphPtr.Get());
		}
	}

	const FString SafeName = Asset.AssetName.ToString();
	const FString FilePath = FPaths::Combine(OutDir, FString::Printf(TEXT("BP_%s.txt"), *SafeName));
	FFileHelper::SaveStringToFile(S, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogArchaeoDump, Display, TEXT("Wrote %s (%d chars)"), *FilePath, S.Len());
}

void UArchaeoDumpCommandlet::DumpLevel(const FString& MapPackageName, const FString& OutDir)
{
	UPackage* Package = LoadPackage(nullptr, *MapPackageName, LOAD_None);
	UWorld* World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
	if (World == nullptr)
	{
		UE_LOG(LogArchaeoDump, Error, TEXT("Could not load world %s"), *MapPackageName);
		return;
	}

	FString S;
	S += FString::Printf(TEXT("MAP: %s\n\n"), *MapPackageName);

	int32 ActorCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor == nullptr)
		{
			continue;
		}
		++ActorCount;

		const FVector Location = Actor->GetActorLocation();
		const FRotator Rotation = Actor->GetActorRotation();
		const FVector Scale = Actor->GetActorScale3D();
		const FBox Box = Actor->GetComponentsBoundingBox(true);

		S += FString::Printf(TEXT("[%s]  label=\"%s\"  loc=(%.1f, %.1f, %.1f)  rot=(P%.1f Y%.1f R%.1f)  scale=(%.2f, %.2f, %.2f)  zSize=%.1f\n"),
			*Actor->GetClass()->GetName(),
			*Actor->GetActorLabel(),
			Location.X, Location.Y, Location.Z,
			Rotation.Pitch, Rotation.Yaw, Rotation.Roll,
			Scale.X, Scale.Y, Scale.Z,
			Box.GetSize().Z);

		TArray<UStaticMeshComponent*> MeshComponents;
		Actor->GetComponents<UStaticMeshComponent>(MeshComponents);
		for (UStaticMeshComponent* MeshComp : MeshComponents)
		{
			if (MeshComp == nullptr || MeshComp->GetStaticMesh() == nullptr)
			{
				continue;
			}
			const FString MeshPath = MeshComp->GetStaticMesh()->GetPathName();
			const FVector MeshScale = MeshComp->GetComponentScale();
			S += FString::Printf(TEXT("      mesh[%s] = %s  scale=(%.2f, %.2f, %.2f)\n"),
				*MeshComp->GetName(), *MeshPath, MeshScale.X, MeshScale.Y, MeshScale.Z);
		}
	}

	S += FString::Printf(TEXT("\nTOTAL ACTORS: %d\n"), ActorCount);

	const FString FilePath = FPaths::Combine(OutDir, FString::Printf(TEXT("LEVEL_%s.txt"), *FPaths::GetBaseFilename(MapPackageName)));
	FFileHelper::SaveStringToFile(S, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogArchaeoDump, Display, TEXT("Wrote %s (actors=%d)"), *FilePath, ActorCount);
}

int32 UArchaeoDumpCommandlet::Main(const FString& Params)
{
	FString OutDir;
	FString Mode;
	FString MapParam;

	FParse::Value(*Params, TEXT("out="), OutDir);
	FParse::Value(*Params, TEXT("filter="), Filter);
	FParse::Value(*Params, TEXT("mode="), Mode);
	FParse::Value(*Params, TEXT("map="), MapParam);

	if (OutDir.IsEmpty())
	{
		OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ArchaeoDump"));
	}
	if (Mode.IsEmpty())
	{
		Mode = TEXT("bp");
	}
	IFileManager::Get().MakeDirectory(*OutDir, true);

	UE_LOG(LogArchaeoDump, Display, TEXT("OutDir=%s Mode=%s Filter=%s"), *OutDir, *Mode, Filter.IsEmpty() ? TEXT("<none>") : *Filter);

	if (Mode == TEXT("bp") || Mode == TEXT("all"))
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		FARFilter AssetFilter;
		AssetFilter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		AssetFilter.bRecursiveClasses = true;
		AssetFilter.PackagePaths.Add(TEXT("/Game"));
		AssetFilter.bRecursivePaths = true;

		TArray<FAssetData> Assets;
		AssetRegistry.GetAssets(AssetFilter, Assets);
		UE_LOG(LogArchaeoDump, Display, TEXT("Found %d blueprint assets"), Assets.Num());

		for (const FAssetData& Asset : Assets)
		{
			const FString PackageName = Asset.PackageName.ToString();
			if (!Filter.IsEmpty() && !PackageName.Contains(Filter))
			{
				continue;
			}
			DumpBlueprintAsset(Asset, OutDir);
		}
	}

	if (Mode == TEXT("level") || Mode == TEXT("all"))
	{
		TArray<FString> Maps;
		if (!MapParam.IsEmpty())
		{
			Maps.Add(MapParam);
		}
		else
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

			FARFilter MapFilter;
			MapFilter.ClassPaths.Add(UWorld::StaticClass()->GetClassPathName());
			MapFilter.bRecursiveClasses = true;
			MapFilter.PackagePaths.Add(TEXT("/Game"));
			MapFilter.bRecursivePaths = true;

			TArray<FAssetData> MapAssets;
			AssetRegistry.GetAssets(MapFilter, MapAssets);
			for (const FAssetData& MapAsset : MapAssets)
			{
				Maps.Add(MapAsset.PackageName.ToString());
			}
		}

		for (const FString& MapName : Maps)
		{
			if (!Filter.IsEmpty() && !MapName.Contains(Filter))
			{
				continue;
			}
			DumpLevel(MapName, OutDir);
		}
	}

	UE_LOG(LogArchaeoDump, Display, TEXT("Done."));
	return 0;
}
