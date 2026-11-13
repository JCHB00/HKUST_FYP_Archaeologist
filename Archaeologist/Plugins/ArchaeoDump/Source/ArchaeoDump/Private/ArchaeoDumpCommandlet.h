#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "ArchaeoDumpCommandlet.generated.h"

/**
 * Editor-only commandlet that writes readable text dumps of
 *  - every Blueprint graph (nodes, pins, default values, links, variables, SCS components)
 *  - every level's actor layout (class, label, transform, static meshes)
 *
 * Usage:
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=ArchaeoDump -out=<dir> -mode=all|bp|level -filter=<substring>
 */
UCLASS()
class UArchaeoDumpCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UArchaeoDumpCommandlet();

	virtual int32 Main(const FString& Params) override;

private:
	void DumpBlueprintAsset(const struct FAssetData& Asset, const FString& OutDir);
	void DumpLevel(const FString& MapPackageName, const FString& OutDir);
	static FString GraphToText(class UEdGraph* Graph);
	static FString PinTypeToString(const struct FEdGraphPinType& PinType);

	FString Filter;
};
