---
name: blueprint-to-cpp
description: Generate C++ header and source files from a Blueprint asset
version: 1.0.0
---

# Blueprint to C++

Convert an Unreal Engine Blueprint asset into C++ `.h` and `.cpp` files.

This skill has two layers:
- **Layer 1 (Scaffolding):** Class declaration, properties, function signatures, components, constructor
- **Layer 2 (Logic Translation):** Convert Blueprint graph nodes into C++ function bodies

> **Important:** The generated C++ is a best-effort translation, not a guaranteed 1:1 conversion.
> Some Blueprint constructs have no direct C++ equivalent, and the translation may not capture
> every nuance of complex graphs. Review the output carefully — nodes that could not be translated
> are marked with `// TODO:` comments for manual implementation. Always compile and test the
> generated code before using it in production.

## Layer 1: Scaffolding

### Step 1 — Query the Blueprint

```
soft-ue-cli query-blueprint <ASSET_PATH> --include all --include-inherited
```

### Step 2 — Generate the `.h` Header

From the JSON response, build the header file:

1. **File boilerplate:**
   - `#pragma once`
   - `#include "CoreMinimal.h"` (first include)
   - `#include "<ParentClassHeader>"` (see Parent Class Header Map)
   - Any additional includes for component types, struct types, etc.
   - `#include "<ClassName>.generated.h"` (last include, always)

2. **Class declaration:**
   - `UCLASS()` macro
   - `class <MODULE>_API <ClassName> : public <ParentClass>` — derive from `parent_class`
   - If `interfaces.items[]` is non-empty, append `, public I<InterfaceName>` for each
   - `GENERATED_BODY()` as first line inside class

3. **Components** (from `components.items[]`):
   Place in a `private:` section:
   ```cpp
   UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
   TObjectPtr<U<ComponentClass>> <ComponentName>;
   ```

4. **Variables** (from `variables.items[]`):
   Place in a `public:` section. For each variable:
   ```cpp
   UPROPERTY(<specifiers>)
   <CppType> <VarName>;
   ```
   Map specifiers using the UPROPERTY Specifier Map below.
   Map types using the Type Mapping Table below.
   If `is_array` is true, wrap in `TArray<>`. If `is_set`, wrap in `TSet<>`. If `is_map`, use `TMap<KeyType, ValueType>`.

5. **Functions** (from `functions.items[]`):
   ```cpp
   UFUNCTION(<specifiers>)
   <ReturnType> <FuncName>(<Params>);
   ```
   Map specifiers using the UFUNCTION Specifier Map below.
   Parameters come from `functions.items[].parameters[]` — map each `type`/`sub_type` using the Type Mapping Table.
   If no return parameter, use `void`.

6. **Custom Events** (from `event_graph.custom_events[]`):
   ```cpp
   UFUNCTION()
   void <EventName>(<Params>);
   ```

7. **Replication:**
   If any variable has `replication` == `Replicated` or `ReplicatedUsing`:
   ```cpp
   virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
   ```
   For each `ReplicatedUsing` variable with `rep_notify_func`:
   ```cpp
   UFUNCTION()
   void <rep_notify_func>();
   ```

8. **Constructor:**
   ```cpp
   <ClassName>();
   ```

### Step 3 — Generate the `.cpp` Source

1. **Include the header:**
   ```cpp
   #include "<ClassName>.h"
   ```
   Add `#include "Net/UnrealNetwork.h"` if replication is used.
   Add `#include "Components/<ComponentType>.h"` for each component type used.

2. **Constructor:**
   ```cpp
   <ClassName>::<ClassName>()
   {
       // Components
       <CompName> = CreateDefaultSubobject<U<CompClass>>(TEXT("<CompName>"));
       // If is_root: SetRootComponent(<CompName>);
       // If parent component exists: <CompName>->SetupAttachment(<ParentCompName>);

       // Default values from defaults.properties[]
       <VarName> = <default_value>;
   }
   ```

3. **GetLifetimeReplicatedProps** (if replicated variables exist):
   ```cpp
   void <ClassName>::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
   {
       Super::GetLifetimeReplicatedProps(OutLifetimeProps);
       DOREPLIFETIME(<ClassName>, <VarName>);
       // For ReplicatedUsing: DOREPLIFETIME_CONDITION(<ClassName>, <VarName>, COND_None);
   }
   ```

4. **Function bodies:**
   For Layer 1 only, emit empty stubs:
   ```cpp
   void <ClassName>::<FuncName>(<Params>)
   {
       // TODO: implement
   }
   ```
   Then proceed to Layer 2 to fill these in.

---

## Layer 2: Logic Translation

### Step 4 — List All Callables

```
soft-ue-cli query-blueprint-graph <ASSET_PATH> --list-callables
```

This returns `events`, `functions`, and `macros` with their names and parameters.

### Step 5 — For Each Function/Event, Query Its Graph

```
soft-ue-cli query-blueprint-graph <ASSET_PATH> --callable-name <NAME>
```

### Step 6 — Walk the Graph and Translate

**Algorithm:**

There are two types of connections in a Blueprint graph:
- **Execution flows** (white wires): connect exec pins, determine statement order
- **Data flows** (colored wires): connect data pins, determine expression values

Walk the graph in two passes per statement:

1. **Find entry nodes:** `K2Node_Event`, `K2Node_FunctionEntry`, `K2Node_CustomEvent`
2. **Follow execution flow:** Trace `then`/`execute` output pins via `connections[].node_guid` to determine the sequence of statements
3. **Resolve data expressions:** For each exec node's data input pins, trace backwards through pure (non-exec) data nodes to build the C++ expression. Pure nodes have no exec pins — they are inlined as expressions, not statements.
4. **Deduplicate pure nodes:** If a pure node's output connects to 2+ consumer pins, evaluate it once into a local variable (see Optimization Rules)
5. **Emit C++ statements** in execution order
6. **Handle branching:** Branch → `if`/`else`, follow both exec paths
7. **Handle sequences:** Sequence → emit statements in output pin order (Then_0, Then_1, ...)
8. **Handle loops:** ForLoop/WhileLoop → emit loop construct, follow Loop Body exec pin
9. **Skip reroute nodes:** `K2Node_Knot` — pass through to the connected node
10. **Replace stubs:** Fill in the `// TODO: implement` bodies from Layer 1

---

## Type Mapping Table

| Blueprint Type | C++ Type |
|---|---|
| `bool` | `bool` |
| `byte` | `uint8` |
| `int32` | `int32` |
| `int64` | `int64` |
| `float` | `float` |
| `double` | `double` |
| `FName` | `FName` |
| `FString` | `FString` |
| `FText` | `FText` |
| `FVector` | `FVector` |
| `FVector2D` | `FVector2D` |
| `FRotator` | `FRotator` |
| `FTransform` | `FTransform` |
| `FLinearColor` | `FLinearColor` |
| `FColor` | `FColor` |
| `FGameplayTag` | `FGameplayTag` |
| `FGameplayTagContainer` | `FGameplayTagContainer` |
| `object` with sub_type `<Class>` | `TObjectPtr<U<Class>>` — use `A<Class>*` if sub_type starts with `A` (Actor subclass) |
| `class` with sub_type `<Class>` | `TSubclassOf<U<Class>>` |
| `softobject` with sub_type `<Class>` | `TSoftObjectPtr<U<Class>>` |
| `softclass` with sub_type `<Class>` | `TSoftClassPtr<U<Class>>` |
| `struct` with sub_type `<Struct>` | `F<Struct>` — prefix with `F` if not already |
| `enum` with sub_type `<Enum>` | `E<Enum>` — prefix with `E` if not already |
| `delegate` | `FScriptDelegate` |
| `mcdelegate` | `FMulticastScriptDelegate` |

**Container wrappers:** If `is_array` is true → `TArray<InnerType>`. If `is_set` → `TSet<InnerType>`. If `is_map` → `TMap<KeyType, ValueType>`.

---

## Parent Class Header Map

| Parent Class | Include Path |
|---|---|
| `Actor` | `GameFramework/Actor.h` |
| `Character` | `GameFramework/Character.h` |
| `Pawn` | `GameFramework/Pawn.h` |
| `PlayerController` | `GameFramework/PlayerController.h` |
| `AIController` | `AIController.h` |
| `GameModeBase` | `GameFramework/GameModeBase.h` |
| `GameMode` | `GameFramework/GameMode.h` |
| `GameStateBase` | `GameFramework/GameStateBase.h` |
| `PlayerState` | `GameFramework/PlayerState.h` |
| `HUD` | `GameFramework/HUD.h` |
| `ActorComponent` | `Components/ActorComponent.h` |
| `SceneComponent` | `Components/SceneComponent.h` |
| `UserWidget` | `Blueprint/UserWidget.h` |
| `AnimInstance` | `Animation/AnimInstance.h` |
| `GameInstanceSubsystem` | `Subsystems/GameInstanceSubsystem.h` |
| `WorldSubsystem` | `Subsystems/WorldSubsystem.h` |

For classes not in this table, derive from `parent_class_path`:
- Strip `/Script/` prefix and module name → use the class name
- Add `// TODO: verify include path for <ParentClass>` comment

---

## UPROPERTY Specifier Map

Build the `UPROPERTY(...)` specifier list from the variable's JSON fields:

| JSON Field | Specifier |
|---|---|
| `flags` contains `EditAnywhere` | `EditAnywhere` |
| `flags` contains `EditDefaultsOnly` | `EditDefaultsOnly` |
| `flags` contains `EditInstanceOnly` | `EditInstanceOnly` |
| `flags` contains `VisibleAnywhere` | `VisibleAnywhere` |
| `flags` contains `BlueprintReadWrite` | `BlueprintReadWrite` |
| `flags` contains `BlueprintReadOnly` | `BlueprintReadOnly` |
| `flags` contains `ExposeOnSpawn` | `ExposeOnSpawn` |
| `replication` == `Replicated` | `Replicated` |
| `replication` == `ReplicatedUsing` | `ReplicatedUsing=OnRep_<VarName>` (or use `rep_notify_func` if present) |
| `category` is non-empty | `Category = "<category>"` |

If no edit/visible specifier is present, default to `EditAnywhere, BlueprintReadWrite`.

---

## UFUNCTION Specifier Map

| JSON Flag | Specifier |
|---|---|
| `BlueprintCallable` | `BlueprintCallable` |
| `BlueprintPure` | `BlueprintPure` |
| `BlueprintNativeEvent` | `BlueprintNativeEvent` |
| `BlueprintImplementableEvent` | `BlueprintImplementableEvent` |
| `BlueprintAuthorityOnly` | `BlueprintAuthorityOnly` |
| `NetMulticast` | `NetMulticast` |
| `Server` | `Server` |
| `Client` | `Client` |
| `Reliable` | `Reliable` |
| `Unreliable` | `Unreliable` |

Always add `Category = "Default"` if no category is specified and the function is `BlueprintCallable`.

---

## Node Class → C++ Translation

### Variable Access

| Node Class | C++ |
|---|---|
| `K2Node_VariableGet` | `VarName` |
| `K2Node_VariableSet` | `VarName = <input>;` |
| `K2Node_StructMemberGet` | `StructVar.MemberName` |
| `K2Node_StructMemberSet` | `StructVar.MemberName = <value>;` |
| `K2Node_Self` | `this` |

### Flow Control

| Node Class | C++ |
|---|---|
| `K2Node_IfThenElse` | `if (<Condition>) { <True branch> } else { <False branch> }` |
| `K2Node_ExecutionSequence` | Emit statements in pin order: Then_0, Then_1, ... |
| `K2Node_ForLoop` | `for (int32 Index = <FirstIndex>; Index <= <LastIndex>; ++Index) { <LoopBody> }` |
| `K2Node_ForLoopWithBreak` | Same as ForLoop, add `break` when Break pin is connected |
| `K2Node_ForEachElementInArray` | `for (auto& Element : <Array>) { <LoopBody> }` |
| `K2Node_WhileLoop` | `while (<Condition>) { <LoopBody> }` |
| `K2Node_Select` | `(<Condition> ? <OptionA> : <OptionB>)` or `switch` for multiple options |
| `K2Node_SwitchInteger` | `switch (<Value>) { case 0: <...>; break; ... default: <...>; }` |
| `K2Node_SwitchString` | `if (<Value> == TEXT("<Case>")) { ... } else if ...` |
| `K2Node_SwitchName` | `if (<Value> == FName(TEXT("<Case>"))) { ... } else if ...` |
| `K2Node_SwitchEnum` | `switch (<Value>) { case E<Enum>::<Val>: <...>; break; ... }` |
| `K2Node_FlipFlop` | `bFlipFlop_<ID> = !bFlipFlop_<ID>; if (bFlipFlop_<ID>) { <A> } else { <B> }` — add `bool bFlipFlop_<ID> = false;` as class member |
| `K2Node_DoOnce` | `if (!bDoOnce_<ID>) { bDoOnce_<ID> = true; <Body> }` — add `bool bDoOnce_<ID> = false;` as class member, connect Reset pin to `bDoOnce_<ID> = false;` |
| `K2Node_Gate` | `if (bGate_<ID>) { <Body> }` — Open: `bGate_<ID> = true;` Close: `bGate_<ID> = false;` Toggle: `bGate_<ID> = !bGate_<ID>;` — add `bool bGate_<ID> = false;` as class member |
| `K2Node_DoN` | `if (DoNCount_<ID> < <N>) { ++DoNCount_<ID>; <Body> }` — add `int32 DoNCount_<ID> = 0;` as class member, connect Reset to `DoNCount_<ID> = 0;` |
| `K2Node_MultiGate` | `switch (MultiGateIndex_<ID>++) { case 0: <Out0>; break; case 1: <Out1>; break; ... }` — add `int32 MultiGateIndex_<ID> = 0;` as class member |
| `K2Node_IsValid` | `if (IsValid(<Object>))` or `if (<Object> != nullptr)` |

### Math & Operators

| Pattern | C++ |
|---|---|
| Title: `Add` / `Subtract` / `Multiply` / `Divide` | `<A> + <B>`, `<A> - <B>`, `<A> * <B>`, `<A> / <B>` |
| Title: `Equal` / `NotEqual` | `<A> == <B>`, `<A> != <B>` |
| Title: `Greater` / `Less` / `GreaterEqual` / `LessEqual` | `<A> > <B>`, `<A> < <B>`, `<A> >= <B>`, `<A> <= <B>` |
| `AND_Boolean` / `OR_Boolean` / `NOT_Boolean` | `<A> && <B>`, `<A> \|\| <B>`, `!<A>` |
| `Clamp` | `FMath::Clamp(<Value>, <Min>, <Max>)` |
| `Lerp` | `FMath::Lerp(<A>, <B>, <Alpha>)` |
| `FInterpTo` | `FMath::FInterpTo(<Current>, <Target>, <DeltaTime>, <Speed>)` |
| `Abs` / `Sin` / `Cos` / `Sqrt` / `Pow` | `FMath::Abs(<V>)`, `FMath::Sin(<V>)`, etc. |
| `RandomFloat` / `RandomInteger` / `RandRange` | `FMath::FRand()`, `FMath::RandRange(<Min>, <Max>)` |
| `MakeVector` | `FVector(<X>, <Y>, <Z>)` |
| `BreakVector` | `<Vec>.X`, `<Vec>.Y`, `<Vec>.Z` |
| `MakeRotator` | `FRotator(<Pitch>, <Yaw>, <Roll>)` |
| `BreakRotator` | `<Rot>.Pitch`, `<Rot>.Yaw`, `<Rot>.Roll` |
| `MakeTransform` | `FTransform(<Rotation>, <Location>, <Scale>)` |
| `BreakTransform` | `<T>.GetLocation()`, `<T>.GetRotation()`, `<T>.GetScale3D()` |
| `Normalize` | `<Vec>.GetSafeNormal()` |
| `VectorLength` | `<Vec>.Size()` |
| `DotProduct` | `FVector::DotProduct(<A>, <B>)` |
| `CrossProduct` | `FVector::CrossProduct(<A>, <B>)` |
| `Negate` / `UnaryMinus` | `-<Value>` |

### Casting

| Node Class | C++ |
|---|---|
| `K2Node_DynamicCast` | `<TargetClass>* CastResult = Cast<<TargetClass>>(<Object>); if (CastResult) { <Success> } else { <Fail> }` |
| `K2Node_ClassDynamicCast` | Same pattern for class references |
| Title `IsA` | `<Object>->IsA<<TargetClass>>()` |
| Title `GetClass` | `<Object>->GetClass()` |

### Function Calls

| Node Class | C++ |
|---|---|
| `K2Node_CallFunction` (self target) | `<FuncName>(<Params>)` |
| `K2Node_CallFunction` (external target) | `<Target>-><FuncName>(<Params>)` |
| `K2Node_CallFunction` (static/library) | `U<ClassName>::<FuncName>(<Params>)` |
| `K2Node_CallArrayFunction` | `<Array>.<Method>(<Params>)` |
| `K2Node_CallParentFunction` | `Super::<FuncName>(<Params>)` |

#### Common Library Calls

| Blueprint Function | C++ |
|---|---|
| `PrintString` | `UKismetSystemLibrary::PrintString(this, <Text>)` |
| `GetPlayerController` | `UGameplayStatics::GetPlayerController(this, <Index>)` |
| `GetPlayerCharacter` | `UGameplayStatics::GetPlayerCharacter(this, <Index>)` |
| `GetPlayerPawn` | `UGameplayStatics::GetPlayerPawn(this, <Index>)` |
| `GetGameMode` | `UGameplayStatics::GetGameMode(this)` |
| `GetGameInstance` | `UGameplayStatics::GetGameInstance(this)` |
| `GetWorldDeltaSeconds` | `UGameplayStatics::GetWorldDeltaSeconds(this)` |
| `GetTimeSeconds` | `UGameplayStatics::GetTimeSeconds(this)` |
| `SpawnActor` | `GetWorld()->SpawnActor<<Class>>(<Class>::StaticClass(), <Location>, <Rotation>)` |
| `DestroyActor` | `<Actor>->Destroy()` |
| `SetActorLocation` | `SetActorLocation(<Location>)` |
| `GetActorLocation` | `GetActorLocation()` |
| `SetActorRotation` | `SetActorRotation(<Rotation>)` |
| `GetActorRotation` | `GetActorRotation()` |
| `SetActorTransform` | `SetActorTransform(<Transform>)` |
| `GetActorTransform` | `GetActorTransform()` |
| `AddActorWorldOffset` | `AddActorWorldOffset(<Delta>)` |
| `AddActorWorldRotation` | `AddActorWorldRotation(<DeltaRot>)` |
| `PlaySound2D` | `UGameplayStatics::PlaySound2D(this, <Sound>)` |
| `PlaySoundAtLocation` | `UGameplayStatics::PlaySoundAtLocation(this, <Sound>, <Location>)` |
| `SpawnSoundAtLocation` | `UGameplayStatics::SpawnSoundAtLocation(this, <Sound>, <Location>)` |
| `ApplyDamage` | `UGameplayStatics::ApplyDamage(<Target>, <Damage>, <InstigatorController>, <DamageCauser>, <DamageType>)` |
| `ApplyPointDamage` | `UGameplayStatics::ApplyPointDamage(...)` |
| `ApplyRadialDamage` | `UGameplayStatics::ApplyRadialDamage(...)` |
| `LineTraceByChannel` | `GetWorld()->LineTraceSingleByChannel(<HitResult>, <Start>, <End>, <Channel>)` |
| `SphereTraceByChannel` | `GetWorld()->SweepSingleByChannel(<HitResult>, <Start>, <End>, <Rotation>, <Channel>, FCollisionShape::MakeSphere(<Radius>))` |
| `SetTimerByFunctionName` | `GetWorldTimerManager().SetTimer(<Handle>, this, &ThisClass::<Func>, <Rate>, <bLoop>)` |
| `ClearTimer` | `GetWorldTimerManager().ClearTimer(<Handle>)` |
| `SetTimerByEvent` | `GetWorldTimerManager().SetTimer(<Handle>, <Delegate>, <Rate>, <bLoop>)` |
| `SetVisibility` | `<Component>->SetVisibility(<bVisible>)` |
| `SetActorHiddenInGame` | `SetActorHiddenInGame(<bHidden>)` |
| `SetCollisionEnabled` | `<Component>->SetCollisionEnabled(<Type>)` |
| `SetActorEnableCollision` | `SetActorEnableCollision(<bEnabled>)` |
| `SetActorTickEnabled` | `SetActorTickEnabled(<bEnabled>)` |
| `SetComponentTickEnabled` | `<Component>->SetComponentTickEnabled(<bEnabled>)` |
| `GetAllActorsOfClass` | `UGameplayStatics::GetAllActorsOfClass(this, <Class>::StaticClass(), <OutActors>)` |
| `IsLocallyControlled` | `IsLocallyControlled()` |
| `HasAuthority` | `HasAuthority()` |
| `GetOwner` | `GetOwner()` |
| `SetOwner` | `SetOwner(<NewOwner>)` |

### Array Operations

| Node Title | C++ |
|---|---|
| `Add` | `<Array>.Add(<Item>)` |
| `Add Unique` | `<Array>.AddUnique(<Item>)` |
| `Remove` (by index) | `<Array>.RemoveAt(<Index>)` |
| `Remove Item` | `<Array>.Remove(<Item>)` |
| `Get` / `Array Element` | `<Array>[<Index>]` |
| `Length` / `Num` | `<Array>.Num()` |
| `Contains` | `<Array>.Contains(<Item>)` |
| `Find` | `<Array>.Find(<Item>)` |
| `Clear` | `<Array>.Empty()` |
| `Sort` | `<Array>.Sort()` |
| `IsEmpty` | `<Array>.IsEmpty()` |
| `Last Index` | `<Array>.Num() - 1` |
| `Last` | `<Array>.Last()` |
| `Insert` | `<Array>.Insert(<Item>, <Index>)` |
| `Shuffle` | `Algo::RandomShuffle(<Array>)` |
| `Reverse` | `Algo::Reverse(<Array>)` |
| `SetArrayElem` | `<Array>[<Index>] = <Value>` |
| `Resize` | `<Array>.SetNum(<NewSize>)` |

### String Operations

| Node Title | C++ |
|---|---|
| `Append` | `<Str1> + <Str2>` |
| `Len` | `<Str>.Len()` |
| `Contains` | `<Str>.Contains(<Sub>)` |
| `StartsWith` | `<Str>.StartsWith(<Prefix>)` |
| `EndsWith` | `<Str>.EndsWith(<Suffix>)` |
| `Equals` (case insensitive) | `<Str>.Equals(<Other>, ESearchCase::IgnoreCase)` |
| `Left` / `Right` / `Mid` | `<Str>.Left(<N>)` / `<Str>.Right(<N>)` / `<Str>.Mid(<Start>, <Count>)` |
| `ToUpper` / `ToLower` | `<Str>.ToUpper()` / `<Str>.ToLower()` |
| `TrimStartAndEnd` | `<Str>.TrimStartAndEnd()` |
| `ParseIntoArray` | `<Str>.ParseIntoArray(<OutArray>, TEXT("<Delim>"))` |
| `Conv_IntToString` | `FString::FromInt(<Value>)` |
| `Conv_FloatToString` | `FString::SanitizeFloat(<Value>)` |
| `Conv_StringToInt` | `FCString::Atoi(*<Str>)` |
| `Conv_StringToFloat` | `FCString::Atof(*<Str>)` |
| `Conv_BoolToString` | `<Value> ? TEXT("true") : TEXT("false")` |
| `Printf` / `Format` | `FString::Printf(TEXT("<Format>"), <Args>...)` |

### Map Operations

| Node Title | C++ |
|---|---|
| `Add` | `<Map>.Add(<Key>, <Value>)` |
| `Remove` | `<Map>.Remove(<Key>)` |
| `Find` | `<Map>.Find(<Key>)` (returns pointer) |
| `Contains` | `<Map>.Contains(<Key>)` |
| `Keys` | `TArray<KeyType> Keys; <Map>.GetKeys(Keys)` |
| `Values` | `TArray<ValueType> Values; <Map>.GenerateValueArray(Values)` |
| `Length` | `<Map>.Num()` |
| `IsEmpty` | `<Map>.IsEmpty()` |
| `Clear` | `<Map>.Empty()` |

### Set Operations

| Node Title | C++ |
|---|---|
| `Add` | `<Set>.Add(<Item>)` |
| `Remove` | `<Set>.Remove(<Item>)` |
| `Contains` | `<Set>.Contains(<Item>)` |
| `Length` | `<Set>.Num()` |
| `ToArray` | `<Set>.Array()` |
| `Clear` | `<Set>.Empty()` |
| `Union` | `<SetA>.Union(<SetB>)` |
| `Intersection` | `<SetA>.Intersect(<SetB>)` |
| `Difference` | `<SetA>.Difference(<SetB>)` |

### Async & Latent Actions

| Node Class | C++ |
|---|---|
| `K2Node_Delay` | Split function at this point. Add `FTimerHandle TimerHandle_<ID>;` as class member. Emit: `GetWorldTimerManager().SetTimer(TimerHandle_<ID>, this, &ThisClass::<ContinuationFunc>, <Duration>, false);` Create a new function `<ContinuationFunc>()` containing the code after the Delay. |
| `K2Node_RetriggerableDelay` | Same as Delay — reusing the same FTimerHandle auto-clears the previous timer. |
| `K2Node_Timeline` | Add `FTimeline Timeline_<Name>;` as class member. In constructor or BeginPlay, set up curve keys and bind: `Timeline_<Name>.SetTimelineLength(<Length>); Timeline_<Name>.SetLooping(<bLoop>);` Bind Update/Finished delegates. In Tick: `Timeline_<Name>.TickTimeline(<DeltaTime>);` Add `// TODO: set up curve assets and bind Update/Finished delegates for Timeline_<Name>` |
| `K2Node_LatentGameplayTaskCall` | `U<TaskClass>* Task = U<TaskClass>::<FactoryFunc>(this, <Params>); Task->ReadyForActivation();` Bind output delegate pins to handler functions. |
| `K2Node_LatentAbilityCall` | Same pattern as LatentGameplayTaskCall for GAS ability tasks. |

**Latent split pattern:** When a Delay or latent node appears mid-function:
1. End the current function body at the latent point
2. Create a new `UFUNCTION()` named `<OriginalFunc>_AfterDelay` (or similar)
3. Move all code after the latent node into the continuation function
4. Add `// NOTE: Split from Blueprint latent node — was a single execution flow in BP` comment

### Events & Delegates

| Node Class | C++ |
|---|---|
| `K2Node_Event` (BeginPlay) | `virtual void BeginPlay() override;` → `void <Class>::BeginPlay() { Super::BeginPlay(); <Body> }` |
| `K2Node_Event` (Tick) | `virtual void Tick(float DeltaTime) override;` → `void <Class>::Tick(float DeltaTime) { Super::Tick(DeltaTime); <Body> }` |
| `K2Node_Event` (EndPlay) | `virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;` |
| `K2Node_Event` (other native) | `virtual void <EventName>(<Params>) override;` with `Super::<EventName>(<Params>);` as first line |
| `K2Node_CustomEvent` | `void <EventName>(<Params>) { <Body> }` |
| `K2Node_ComponentBoundEvent` | In BeginPlay: `<Component>->On<Event>.AddDynamic(this, &ThisClass::<Handler>);` Add `UFUNCTION() void <Handler>(<EventParams>);` |
| `K2Node_ActorBoundEvent` | `<Actor>->On<Event>.AddDynamic(this, &ThisClass::<Handler>);` |
| `K2Node_CreateDelegate` | `FScriptDelegate Delegate; Delegate.BindUFunction(this, FName(TEXT("<FuncName>")));` |
| `K2Node_AssignDelegate` | `<Target>->On<Event>.AddDynamic(this, &ThisClass::<Handler>);` |
| Multicast delegate Broadcast | `On<Event>.Broadcast(<Params>);` |
| `K2Node_InputAction` | In SetupPlayerInputComponent: `<InputComponent>->BindAction(TEXT("<ActionName>"), <Trigger>, this, &ThisClass::<Handler>);` |
| `K2Node_InputKey` | `<InputComponent>->BindKey(<Key>, <Event>, this, &ThisClass::<Handler>);` |
| `K2Node_InputAxisEvent` | `<InputComponent>->BindAxis(TEXT("<AxisName>"), this, &ThisClass::<Handler>);` |

### Construction Script

| Node Class | C++ |
|---|---|
| `K2Node_FunctionEntry` in UserConstructionScript | Place body in `virtual void OnConstruction(const FTransform& Transform) override;` — call `Super::OnConstruction(Transform);` first |

### Make/Break Structs

| Node Class | C++ |
|---|---|
| `K2Node_MakeStruct` | `F<Struct> Var; Var.<Field1> = <Val1>; Var.<Field2> = <Val2>;` |
| `K2Node_BreakStruct` | Access fields directly: `<Struct>.<FieldName>` |
| `K2Node_SetFieldsInStruct` | `<Struct>.<Field> = <Value>;` per field |
| `K2Node_MakeArray` | `TArray<<Type>> Arr = { <Elem0>, <Elem1>, ... };` |
| `K2Node_MakeLiteralBool` | `true` / `false` |
| `K2Node_MakeLiteralInt` | literal integer value |
| `K2Node_MakeLiteralFloat` | literal float value with `f` suffix |
| `K2Node_MakeLiteralString` | `TEXT("<value>")` |
| `K2Node_MakeLiteralText` | `FText::FromString(TEXT("<value>"))` |
| `K2Node_EnumLiteral` | `E<Enum>::<Value>` |

### Misc Nodes

| Node Class | C++ |
|---|---|
| `K2Node_Knot` (Reroute) | Skip — follow through to the connected node |
| `K2Node_Tunnel` / `K2Node_MacroInstance` | Inline the macro body, or emit `// TODO: inline macro "<MacroName>"` if graph is unavailable |
| `K2Node_SpawnActorFromClass` | `<ActorClass>* NewActor = GetWorld()->SpawnActor<<ActorClass>>(<Class>::StaticClass(), <Transform>);` |
| `K2Node_GetDataTableRow` | `F<RowStruct>* Row = <DataTable>->FindRow<F<RowStruct>>(FName(TEXT("<RowName>")), TEXT(""));` |
| `K2Node_CommutativeAssociativeBinaryOperator` | Map to the underlying operation (`+`, `*`, etc.) |

### Unmappable Nodes

If you encounter a node class not listed above:

```cpp
// TODO: translate <NodeClass> "<NodeTitle>"
// Input pins: <pin1> (<type>), <pin2> (<type>), ...
// Output pins: <pin1> (<type>), <pin2> (<type>), ...
```

Continue walking the execution graph past the node.

---

## Code Quality Rules

- Use `ThisClass::` in delegate bindings and timer calls, not the actual class name
- Prefer `TObjectPtr<>` for UPROPERTY object pointers, not raw pointers
- Use `GENERATED_BODY()`, not `GENERATED_UCLASS_BODY()`
- `#include "CoreMinimal.h"` is always the first include
- `#include "<ClassName>.generated.h"` is always the last include in the header
- Group includes: CoreMinimal → Engine → Project headers
- Use `TEXT()` for all string literals
- Mark override functions with `virtual ... override`
- Use `const` on parameters where Blueprint pins are pass-by-reference-const
- Use `Super::` calls as the first line in overridden functions (BeginPlay, Tick, etc.)
- Add `#include "Net/UnrealNetwork.h"` only when replication macros are used
- Declare FlipFlop/DoOnce/Gate/DoN state variables as `UPROPERTY()` class members, not local statics, so they survive across frames correctly

## Optimization Rules (from Blueprint VM analysis)

When translating pure (non-exec) nodes that feed multiple consumers:
- In Blueprints, pure nodes re-execute once per output pin connection (wasteful)
- In C++, evaluate once and store in a local variable:
  ```cpp
  // BAD — mirrors Blueprint re-evaluation
  DoSomething(GetActorLocation());
  DoOther(GetActorLocation()); // called twice

  // GOOD — evaluate once
  const FVector Location = GetActorLocation();
  DoSomething(Location);
  DoOther(Location);
  ```
- Apply this whenever a pure node's output connects to 2+ consumer pins

When translating nested function calls:
- If a function call node references another Blueprint function in the same asset, query its graph too and translate it
- Control depth: translate at most 1 level of nested BP function calls; deeper nesting gets `// TODO: translate nested function <Name>`

---

## Verification

After generating the `.h` and `.cpp` files, verify the output against these checks.

### Structural Checklist

**Header file (.h):**
- [ ] Starts with `#pragma once`
- [ ] `#include "CoreMinimal.h"` is the first include
- [ ] `#include "<ClassName>.generated.h"` is the last include
- [ ] `UCLASS()` macro is present before the class declaration
- [ ] Class inherits from the correct parent class (matches `parent_class` from query)
- [ ] All interfaces from `interfaces.items[]` are listed after the parent class with `public I<Name>`
- [ ] `GENERATED_BODY()` is the first line inside the class body
- [ ] Every variable from `variables.items[]` has a corresponding `UPROPERTY()` member
- [ ] Every function from `functions.items[]` has a corresponding `UFUNCTION()` declaration
- [ ] Every custom event from `event_graph.custom_events[]` has a `UFUNCTION()` declaration
- [ ] Every component from `components.items[]` has a `TObjectPtr<>` member in the private section
- [ ] If any variable has `replication` != `None`, `GetLifetimeReplicatedProps` is declared
- [ ] Constructor is declared

**Source file (.cpp):**
- [ ] Includes the header file as the first include
- [ ] If replication is used, includes `"Net/UnrealNetwork.h"`
- [ ] Constructor creates all components with `CreateDefaultSubobject`
- [ ] Root component is set via `SetRootComponent`
- [ ] Non-root components call `SetupAttachment` with the correct parent
- [ ] Default values from `defaults.properties[]` are assigned in the constructor
- [ ] All function bodies are present (either translated or `// TODO: implement`)
- [ ] Override functions call `Super::` as the first line (BeginPlay, Tick, etc.)
- [ ] `GetLifetimeReplicatedProps` uses `DOREPLIFETIME` for each replicated variable

**Code quality:**
- [ ] No raw pointers for `UPROPERTY` object references — all use `TObjectPtr<>`
- [ ] `TEXT()` macro wraps all string literals
- [ ] `ThisClass::` used in delegate bindings and timer calls (not the class name)
- [ ] Includes are grouped: CoreMinimal → Engine → Project headers
- [ ] FlipFlop/DoOnce/Gate/DoN state variables are class members with `UPROPERTY()`, not local statics

### Example Test Cases

Use these to spot-check that the translation tables are applied correctly.

#### Test Case 1: Simple Actor with Component and Replicated Variable

**Input** (abbreviated `query-blueprint` response):
```json
{
  "name": "BP_HealthPickup",
  "parent_class": "Actor",
  "parent_class_path": "/Script/Engine.Actor",
  "blueprint_type": "Normal",
  "interfaces": { "items": [], "count": 0 },
  "variables": {
    "items": [
      {
        "name": "HealAmount",
        "type": "float",
        "is_array": false, "is_set": false, "is_map": false,
        "replication": "Replicated",
        "category": "Gameplay",
        "flags": ["EditAnywhere", "BlueprintReadWrite"]
      },
      {
        "name": "bIsActive",
        "type": "bool",
        "is_array": false, "is_set": false, "is_map": false,
        "replication": "None",
        "category": "State",
        "flags": ["BlueprintReadOnly"]
      }
    ]
  },
  "functions": {
    "items": [
      {
        "name": "OnPickedUp",
        "flags": ["BlueprintCallable"],
        "parameters": [
          { "name": "Instigator", "type": "object", "sub_type": "APawn" }
        ]
      }
    ]
  },
  "components": {
    "items": [
      { "name": "CollisionSphere", "class": "SphereComponent", "parent": null, "is_root": true },
      { "name": "PickupMesh", "class": "StaticMeshComponent", "parent": "CollisionSphere", "is_root": false }
    ]
  },
  "defaults": {
    "properties": [
      { "name": "HealAmount", "default_value": "50.0" },
      { "name": "bIsActive", "default_value": "true" }
    ]
  }
}
```

**Expected .h:**
```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BP_HealthPickup.generated.h"

UCLASS()
class YOURMODULE_API ABP_HealthPickup : public AActor
{
    GENERATED_BODY()

public:
    ABP_HealthPickup();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Gameplay")
    float HealAmount;

    UPROPERTY(BlueprintReadOnly, Category = "State")
    bool bIsActive;

    UFUNCTION(BlueprintCallable, Category = "Default")
    void OnPickedUp(APawn* Instigator);

private:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USphereComponent> CollisionSphere;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UStaticMeshComponent> PickupMesh;
};
```

**Expected .cpp:**
```cpp
#include "BP_HealthPickup.h"
#include "Net/UnrealNetwork.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"

ABP_HealthPickup::ABP_HealthPickup()
{
    CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
    SetRootComponent(CollisionSphere);

    PickupMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PickupMesh"));
    PickupMesh->SetupAttachment(CollisionSphere);

    HealAmount = 50.0f;
    bIsActive = true;
}

void ABP_HealthPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ABP_HealthPickup, HealAmount);
}

void ABP_HealthPickup::OnPickedUp(APawn* Instigator)
{
    // TODO: implement
}
```

#### Test Case 2: Graph Logic — Branch + Delay + Library Calls

**Input** (abbreviated `query-blueprint-graph` response for `BeginPlay`):
```json
{
  "graph": {
    "name": "EventGraph",
    "type": "event",
    "nodes": [
      {
        "guid": "AAA",
        "class": "K2Node_Event",
        "title": "Event BeginPlay",
        "pins": [
          { "name": "then", "direction": "out", "category": "exec", "connections": [{ "node_guid": "BBB", "pin_name": "execute" }] }
        ]
      },
      {
        "guid": "BBB",
        "class": "K2Node_IfThenElse",
        "title": "Branch",
        "pins": [
          { "name": "execute", "direction": "in", "category": "exec" },
          { "name": "Condition", "direction": "in", "category": "bool", "connections": [{ "node_guid": "CCC", "pin_name": "ReturnValue" }] },
          { "name": "True", "direction": "out", "category": "exec", "connections": [{ "node_guid": "DDD", "pin_name": "execute" }] },
          { "name": "False", "direction": "out", "category": "exec", "connections": [{ "node_guid": "EEE", "pin_name": "execute" }] }
        ]
      },
      {
        "guid": "CCC",
        "class": "K2Node_VariableGet",
        "title": "Get bIsActive",
        "pins": [
          { "name": "ReturnValue", "direction": "out", "category": "bool" }
        ]
      },
      {
        "guid": "DDD",
        "class": "K2Node_CallFunction",
        "title": "Print String",
        "pins": [
          { "name": "execute", "direction": "in", "category": "exec" },
          { "name": "then", "direction": "out", "category": "exec", "connections": [{ "node_guid": "FFF", "pin_name": "execute" }] },
          { "name": "InString", "direction": "in", "category": "FString", "default_value": "Pickup is active!" }
        ]
      },
      {
        "guid": "EEE",
        "class": "K2Node_CallFunction",
        "title": "Destroy Actor",
        "pins": [
          { "name": "execute", "direction": "in", "category": "exec" },
          { "name": "then", "direction": "out", "category": "exec" }
        ]
      },
      {
        "guid": "FFF",
        "class": "K2Node_Delay",
        "title": "Delay",
        "pins": [
          { "name": "execute", "direction": "in", "category": "exec" },
          { "name": "Completed", "direction": "out", "category": "exec", "connections": [{ "node_guid": "GGG", "pin_name": "execute" }] },
          { "name": "Duration", "direction": "in", "category": "float", "default_value": "2.0" }
        ]
      },
      {
        "guid": "GGG",
        "class": "K2Node_CallFunction",
        "title": "Set Visibility",
        "pins": [
          { "name": "execute", "direction": "in", "category": "exec" },
          { "name": "then", "direction": "out", "category": "exec" },
          { "name": "Target", "direction": "in", "category": "object", "connections": [{ "node_guid": "HHH", "pin_name": "ReturnValue" }] },
          { "name": "bNewVisibility", "direction": "in", "category": "bool", "default_value": "false" }
        ]
      },
      {
        "guid": "HHH",
        "class": "K2Node_VariableGet",
        "title": "Get PickupMesh",
        "pins": [
          { "name": "ReturnValue", "direction": "out", "category": "object", "sub_category_object": "StaticMeshComponent" }
        ]
      }
    ]
  }
}
```

**Expected C++ (BeginPlay body):**
```cpp
void ABP_HealthPickup::BeginPlay()
{
    Super::BeginPlay();

    if (bIsActive)
    {
        UKismetSystemLibrary::PrintString(this, TEXT("Pickup is active!"));
        // NOTE: Split from Blueprint latent node — was a single execution flow in BP
        GetWorldTimerManager().SetTimer(TimerHandle_BeginPlay, this, &ThisClass::BeginPlay_AfterDelay, 2.0f, false);
    }
    else
    {
        Destroy();
    }
}

void ABP_HealthPickup::BeginPlay_AfterDelay()
{
    PickupMesh->SetVisibility(false);
}
```

This also requires adding to the header:
```cpp
    FTimerHandle TimerHandle_BeginPlay;

    UFUNCTION()
    void BeginPlay_AfterDelay();
```

#### Test Case 3: ForLoop + Array Operation

**Input** (abbreviated graph for a custom function `ClearInventory`):
```json
{
  "graph": {
    "name": "ClearInventory",
    "type": "function",
    "nodes": [
      {
        "guid": "A1",
        "class": "K2Node_FunctionEntry",
        "title": "ClearInventory",
        "pins": [
          { "name": "then", "direction": "out", "category": "exec", "connections": [{ "node_guid": "A2", "pin_name": "execute" }] }
        ]
      },
      {
        "guid": "A2",
        "class": "K2Node_ForEachElementInArray",
        "title": "For Each Loop",
        "pins": [
          { "name": "execute", "direction": "in", "category": "exec" },
          { "name": "Array", "direction": "in", "category": "object", "is_array": true, "connections": [{ "node_guid": "A3", "pin_name": "ReturnValue" }] },
          { "name": "LoopBody", "direction": "out", "category": "exec", "connections": [{ "node_guid": "A4", "pin_name": "execute" }] },
          { "name": "Array Element", "direction": "out", "category": "object", "sub_category_object": "AItem" },
          { "name": "Completed", "direction": "out", "category": "exec", "connections": [{ "node_guid": "A5", "pin_name": "execute" }] }
        ]
      },
      {
        "guid": "A3",
        "class": "K2Node_VariableGet",
        "title": "Get InventoryItems",
        "pins": [
          { "name": "ReturnValue", "direction": "out", "category": "object", "is_array": true }
        ]
      },
      {
        "guid": "A4",
        "class": "K2Node_CallFunction",
        "title": "Destroy Actor",
        "pins": [
          { "name": "execute", "direction": "in", "category": "exec" },
          { "name": "then", "direction": "out", "category": "exec" },
          { "name": "Target", "direction": "in", "category": "object", "connections": [{ "node_guid": "A2", "pin_name": "Array Element" }] }
        ]
      },
      {
        "guid": "A5",
        "class": "K2Node_CallArrayFunction",
        "title": "Clear",
        "pins": [
          { "name": "execute", "direction": "in", "category": "exec" },
          { "name": "then", "direction": "out", "category": "exec" },
          { "name": "TargetArray", "direction": "in", "category": "object", "is_array": true, "connections": [{ "node_guid": "A3", "pin_name": "ReturnValue" }] }
        ]
      }
    ]
  }
}
```

**Expected C++:**
```cpp
void ABP_HealthPickup::ClearInventory()
{
    for (auto& Element : InventoryItems)
    {
        Element->Destroy();
    }
    InventoryItems.Empty();
}
```
