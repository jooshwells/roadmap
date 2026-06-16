#include "RoadNodeActor.h"
#include "Components/StaticMeshComponent.h"

ARoadNodeActor::ARoadNodeActor()
{
    PrimaryActorTick.bCanEverTick = false;

    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    RootComponent = Mesh;
}

void ARoadNodeActor::BeginPlay()
{
    Super::BeginPlay();
}
