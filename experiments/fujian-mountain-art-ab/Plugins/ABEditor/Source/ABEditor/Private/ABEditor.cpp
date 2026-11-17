// Only bridges the engine's native Landscape import; no terrain generation algorithm.
#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
class FABEditorModule final : public IModuleInterface {
 IConsoleObject* Command=nullptr;
public:
 void StartupModule() override {
  Command=IConsoleManager::Get().RegisterConsoleCommand(TEXT("AB.ImportLandscape"),TEXT("Import the fixed AB sample in B_Landscape only"),FConsoleCommandDelegate::CreateLambda([](){
   UWorld* W=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
   if(!W || W->GetOutermost()->GetName()!=TEXT("/Game/ArtAB/B_Landscape") || !FPaths::ProjectDir().Contains(TEXT("fujian-mountain-art-ab"))) { UE_LOG(LogTemp,Error,TEXT("AB import refused: wrong project/world"));return; }
   for(TActorIterator<ALandscape> It(W);It;++It) { UE_LOG(LogTemp,Error,TEXT("AB import refused: Landscape already exists"));return; }
   TArray<uint8> Bytes;
   const FString File=FPaths::ProjectDir()/TEXT("sample.r16");
   if(!FFileHelper::LoadFileToArray(Bytes,*File) || Bytes.Num()!=253*253*2) { UE_LOG(LogTemp,Error,TEXT("AB import invalid R16"));return; }
   TArray<uint16> Heights; Heights.SetNumUninitialized(253*253); FMemory::Memcpy(Heights.GetData(),Bytes.GetData(),Bytes.Num());
   ALandscape* L=W->SpawnActor<ALandscape>(); L->SetActorLabel(TEXT("AB_Native_Landscape"));
   L->SetActorLocation(FVector(-890560.0,-800089.0365,0)); L->SetActorScale3D(FVector(7067.93650794,6349.9129881,3200.0));
   TMap<FGuid,TArray<uint16>> HeightLayers;HeightLayers.Add(FGuid(),MoveTemp(Heights));
   TMap<FGuid,TArray<FLandscapeImportLayerInfo>> MaterialLayers;MaterialLayers.Add(FGuid(),{});
   L->Import(FGuid::NewGuid(),0,0,252,252,1,63,HeightLayers,*File,MaterialLayers,ELandscapeImportAlphamapType::Additive,TArrayView<const FLandscapeLayer>());
   L->CreateLandscapeInfo(); L->PostEditChange(); L->MarkPackageDirty();
   UE_LOG(LogTemp,Display,TEXT("AB_NATIVE_LANDSCAPE_IMPORTED components=%d"),L->LandscapeComponents.Num());
  }),ECVF_Default);
 }
 void ShutdownModule() override { if(Command)IConsoleManager::Get().UnregisterConsoleObject(Command); }
};
IMPLEMENT_MODULE(FABEditorModule,ABEditor)
