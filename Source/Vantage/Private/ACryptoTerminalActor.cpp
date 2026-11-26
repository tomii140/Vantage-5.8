
#include "ACryptoTerminalActor.h"

// Actores visuales.
#include "ACryptoSlotActor.h"

// Widget de navegación.
#include "CryptoNavigationWidget.h"

// Componentes.
#include "Components/SceneComponent.h"
#include "Components/WidgetComponent.h"

// Sistema financiero.
#include "CryptoFinanceSubsystem.h"

// Unreal Engine.
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"


// ============================================================
// ESTADO ESTÁTICO
// ============================================================

/*
 * El desplazamiento se comparte entre las instancias del terminal.
 * Se conserva el comportamiento de la implementación anterior.
 */
int32 ACryptoTerminalActor::GlobalScrollOffset = 0;


// ============================================================
// CONSTRUCTOR
// ============================================================

/**
 * Configura la jerarquía de componentes del terminal.
 *
 * ROOT:
 * - RootComponent.
 *
 * COMPONENTE DE NAVEGACIÓN:
 * - NavigationWidgetComponent.
 *
 * Los slots continúan siendo actores independientes adjuntados
 * al terminal durante CreateSlotActors().
 *
 * El WidgetComponent de navegación es único y global.
 */
ACryptoTerminalActor::ACryptoTerminalActor()
{
    PrimaryActorTick.bCanEverTick = true;

    // --------------------------------------------------------
    // 1. CREAR ROOT COMPONENT
    // --------------------------------------------------------

    USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(
        TEXT("SceneRoot")
    );

    RootComponent = SceneRoot;

    // --------------------------------------------------------
    // 2. CREAR COMPONENTE GLOBAL DE NAVEGACIÓN
    // --------------------------------------------------------

    NavigationWidgetComponent =
        CreateDefaultSubobject<UWidgetComponent>(
            TEXT("NavigationWidgetComponent")
        );

    NavigationWidgetComponent->SetupAttachment(RootComponent);

    NavigationWidgetComponent->SetWidgetSpace(
        EWidgetSpace::World
    );

    NavigationWidgetComponent->SetDrawSize(
        FVector2D(500.0f, 120.0f)
    );

    NavigationWidgetComponent->SetPivot(
        FVector2D(0.5f, 0.5f)
    );

    /*
     * Posición provisional:
     * debajo del conjunto de slots.
     *
     * Se puede modificar desde el editor sin alterar
     * la lógica de paginación.
     */
    NavigationWidgetComponent->SetRelativeLocation(
        FVector(0.0f, 0.0f, -180.0f)
    );
}


// ============================================================
// CICLO DE VIDA
// ============================================================

/**
 * Inicializa la navegación, obtiene el Subsystem financiero,
 * registra el delegado y crea los slots visuales.
 */
void ACryptoTerminalActor::BeginPlay()
{
    Super::BeginPlay();

    // --------------------------------------------------------
    // 1. INICIALIZAR NAVEGACIÓN GLOBAL
    // --------------------------------------------------------

    InitializeNavigationWidget();

    // --------------------------------------------------------
    // 2. OBTENER EL SUBSYSTEM FINANCIERO
    // --------------------------------------------------------

    FinanceSubsystem = ResolveFinanceSubsystem();

    if (!FinanceSubsystem)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage] Terminal: no se pudo obtener CryptoFinanceSubsystem.")
        );

        RefreshNavigationWidget();

        return;
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Vantage][Terminal][TRACE] BeginPlay | Actor: %s | CoinTextureMap entries: %d | Slots solicitados: %d | BaseSlotClass: %s"),
        *GetName(),
        CoinTextureMap.Num(),
        DisplaySlotCount,
        *GetNameSafe(BaseSlotClass.Get())
    );

    // Diagnóstico de las entradas configuradas en el editor.
    for (const TPair<FString, TObjectPtr<UTexture2D>>& Entry : CoinTextureMap)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Vantage][Terminal][TRACE] CoinTextureMap | Key: '%s' | Texture: %s | Valid: %s"),
            *Entry.Key,
            *GetNameSafe(Entry.Value.Get()),
            IsValid(Entry.Value.Get()) ? TEXT("SI") : TEXT("NO")
        );
    }

    // --------------------------------------------------------
    // 3. SUSCRIBIRSE A LAS ACTUALIZACIONES FINANCIERAS
    // --------------------------------------------------------

    FinanceSubsystem->OnCryptoDataUpdated.AddUniqueDynamic(
        this,
        &ACryptoTerminalActor::HandleCryptoDataUpdated
    );

    // --------------------------------------------------------
    // 4. CREAR LOS SLOTS VISUALES
    // --------------------------------------------------------

    if (bSpawnSlotActors)
    {
        CreateSlotActors();
    }

    // --------------------------------------------------------
    // 5. APLICAR LA PRIMERA ACTUALIZACIÓN
    // --------------------------------------------------------

    RefreshTerminal();

    UE_LOG(
        LogTemp,
        Log,
        TEXT(
            "[Vantage] Terminal inicializado. "
            "Slots configurados: %d | Modo: %d"
        ),
        DisplaySlotCount,
        static_cast<int32>(CurrentLayoutMode)
    );
}


/**
 * Desregistra los eventos y libera los actores administrados.
 *
 * La referencia del widget se limpia antes de terminar el ciclo
 * de vida del terminal.
 */
void ACryptoTerminalActor::EndPlay(
    const EEndPlayReason::Type EndPlayReason
)
{
    // --------------------------------------------------------
    // 1. DESREGISTRAR DELEGADO FINANCIERO
    // --------------------------------------------------------

    if (FinanceSubsystem)
    {
        FinanceSubsystem->OnCryptoDataUpdated.RemoveDynamic(
            this,
            &ACryptoTerminalActor::HandleCryptoDataUpdated
        );
    }

    // --------------------------------------------------------
    // 2. LIMPIAR REFERENCIA DEL WIDGET
    // --------------------------------------------------------

    if (NavigationWidgetComponent)
    {
        if (UCryptoNavigationWidget* NavigationWidget =
            Cast<UCryptoNavigationWidget>(
                NavigationWidgetComponent->GetUserWidgetObject()
            ))
        {
            NavigationWidget->SetTerminalReference(nullptr);
        }
    }

    // --------------------------------------------------------
    // 3. DESTRUIR LOS SLOTS ADMINISTRADOS
    // --------------------------------------------------------

    for (ACryptoSlotActor* Slot : SpawnedSlots)
    {
        if (IsValid(Slot))
        {
            Slot->Destroy();
        }
    }

    SpawnedSlots.Empty();

    // --------------------------------------------------------
    // 4. LIBERAR REFERENCIA FINANCIERA
    // --------------------------------------------------------

    FinanceSubsystem = nullptr;

    Super::EndPlay(EndPlayReason);
}


// ============================================================
// INICIALIZACIÓN DEL WIDGET DE NAVEGACIÓN
// ============================================================

/**
 * Inicializa el UserWidget contenido en NavigationWidgetComponent.
 *
 * El componente obtiene WBP_CryptoNavigation desde su propiedad
 * Widget Class configurada en el editor.
 *
 * Después de crear el widget, el terminal le entrega directamente
 * su propia referencia mediante SetTerminalReference(this).
 *
 * No se realizan búsquedas globales de actores.
 */
void ACryptoTerminalActor::InitializeNavigationWidget()
{
    if (!IsValid(NavigationWidgetComponent))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage][Navigation] NavigationWidgetComponent inválido.")
        );

        return;
    }

    // --------------------------------------------------------
    // 1. VERIFICAR CLASE DEL WIDGET
    // --------------------------------------------------------

    if (!NavigationWidgetComponent->GetWidgetClass())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Vantage][Navigation] No hay Widget Class asignada. Configurar WBP_CryptoNavigation.")
        );

        return;
    }

    // --------------------------------------------------------
    // 2. INICIALIZAR EL USER WIDGET
    // --------------------------------------------------------

    NavigationWidgetComponent->InitWidget();

    UUserWidget* UserWidget =
        NavigationWidgetComponent->GetUserWidgetObject();

    if (!IsValid(UserWidget))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage][Navigation] No se pudo crear el UserWidget.")
        );

        return;
    }

    // --------------------------------------------------------
    // 3. COMPROBAR LA CLASE BASE NATIVA
    // --------------------------------------------------------

    UCryptoNavigationWidget* NavigationWidget =
        Cast<UCryptoNavigationWidget>(UserWidget);

    if (!NavigationWidget)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage][Navigation] WBP_CryptoNavigation no utiliza CryptoNavigationWidget como clase padre.")
        );

        return;
    }

    // --------------------------------------------------------
    // 4. ENTREGAR REFERENCIA DIRECTA AL TERMINAL
    // --------------------------------------------------------

    NavigationWidget->SetTerminalReference(this);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[Vantage][Navigation] Widget inicializado y vinculado al terminal: %s"),
        *GetName()
    );
}


/**
 * Actualiza el estado visual del widget de navegación.
 *
 * No busca actores ni consulta el mundo.
 * Utiliza exclusivamente el componente que pertenece al terminal.
 */
void ACryptoTerminalActor::RefreshNavigationWidget()
{
    if (!IsValid(NavigationWidgetComponent))
    {
        return;
    }

    UCryptoNavigationWidget* NavigationWidget =
        Cast<UCryptoNavigationWidget>(
            NavigationWidgetComponent->GetUserWidgetObject()
        );

    if (!IsValid(NavigationWidget))
    {
        return;
    }

    NavigationWidget->RefreshNavigation();
}


// ============================================================
// OBTENCIÓN DEL SUBSYSTEM
// ============================================================

/**
 * Centraliza la obtención del Subsystem financiero.
 * Devuelve nullptr cuando no existe un GameInstance válido.
 */
UCryptoFinanceSubsystem* ACryptoTerminalActor::ResolveFinanceSubsystem()
{
    UGameInstance* GameInstance = GetGameInstance();

    if (!GameInstance)
    {
        return nullptr;
    }

    return GameInstance->GetSubsystem<UCryptoFinanceSubsystem>();
}


// ============================================================
// PAGINACIÓN
// ============================================================

/**
 * Modifica el desplazamiento global.
 *
 * El límite inferior se valida aquí. El límite superior y la alineación
 * con el inicio de una página se corrigen en ClampScrollOffset.
 */
void ACryptoTerminalActor::SetGlobalScrollOffset(int32 NewOffset)
{
    GlobalScrollOffset = FMath::Max(0, NewOffset);
}


/** Devuelve el desplazamiento global actual. */
int32 ACryptoTerminalActor::GetGlobalScrollOffset()
{
    return GlobalScrollOffset;
}


/**
 * Avanza o retrocede una cantidad determinada de páginas.
 *
 * @param PageDelta Cantidad de páginas que se desea recorrer.
 */
void ACryptoTerminalActor::NavigatePage(int32 PageDelta)
{
    if (PageDelta == 0)
    {
        return;
    }

    // --------------------------------------------------------
    // 1. OBTENER EL SUBSYSTEM
    // --------------------------------------------------------

    if (!FinanceSubsystem)
    {
        FinanceSubsystem = ResolveFinanceSubsystem();
    }

    if (!FinanceSubsystem)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Vantage][Navigation] No se puede navegar: FinanceSubsystem inválido.")
        );

        RefreshNavigationWidget();

        return;
    }

    // --------------------------------------------------------
    // 2. OBTENER EL CATÁLOGO ACTUAL
    // --------------------------------------------------------

    TArray<FCryptoAssetData> OrderedAssets;

    FinanceSubsystem->GetOrderedTargetAssets(OrderedAssets);

    if (OrderedAssets.IsEmpty())
    {
        GlobalScrollOffset = 0;

        RefreshTerminal();

        return;
    }

    // --------------------------------------------------------
    // 3. CALCULAR EL TAMAÑO DE CADA PÁGINA
    // --------------------------------------------------------

    const int32 SafeDisplayCount = FMath::Clamp(
        DisplaySlotCount,
        1,
        25
    );

    const int32 TotalPages = FMath::DivideAndRoundUp(
        OrderedAssets.Num(),
        SafeDisplayCount
    );

    // --------------------------------------------------------
    // 4. OBTENER LA PÁGINA ACTUAL
    // --------------------------------------------------------

    const int32 CurrentPage = FMath::Clamp(
        GlobalScrollOffset / SafeDisplayCount,
        0,
        TotalPages - 1
    );

    // --------------------------------------------------------
    // 5. CALCULAR LA NUEVA PÁGINA
    // --------------------------------------------------------

    const int32 NewPage = FMath::Clamp(
        CurrentPage + PageDelta,
        0,
        TotalPages - 1
    );

    if (NewPage == CurrentPage)
    {
        RefreshNavigationWidget();
        return;
    }

    // --------------------------------------------------------
    // 6. CONVERTIR LA PÁGINA EN DESPLAZAMIENTO
    // --------------------------------------------------------

    const int32 NewOffset = NewPage * SafeDisplayCount;

    SetGlobalScrollOffset(NewOffset);

    UE_LOG(
        LogTemp,
        Log,
        TEXT(
            "[Vantage][Navigation] Navegación | "
            "Página anterior: %d | Página nueva: %d | "
            "Total páginas: %d | Offset: %d"
        ),
        CurrentPage + 1,
        NewPage + 1,
        TotalPages,
        NewOffset
    );

    // --------------------------------------------------------
    // 7. ACTUALIZAR LOS SLOTS
    // --------------------------------------------------------

    RefreshTerminal();
}


/**
 * Devuelve el índice de la página actual, comenzando en cero.
 */
int32 ACryptoTerminalActor::GetCurrentPageIndex() const
{
    const int32 SafeDisplayCount = FMath::Clamp(
        DisplaySlotCount,
        1,
        25
    );

    return FMath::Max(
        0,
        GlobalScrollOffset / SafeDisplayCount
    );
}


/**
 * Calcula la cantidad de páginas necesarias para mostrar el catálogo.
 */
int32 ACryptoTerminalActor::GetTotalPageCount()
{
    if (!FinanceSubsystem)
    {
        FinanceSubsystem = ResolveFinanceSubsystem();
    }

    if (!FinanceSubsystem)
    {
        return 1;
    }

    TArray<FCryptoAssetData> OrderedAssets;

    FinanceSubsystem->GetOrderedTargetAssets(OrderedAssets);

    if (OrderedAssets.IsEmpty())
    {
        return 1;
    }

    const int32 SafeDisplayCount = FMath::Clamp(
        DisplaySlotCount,
        1,
        25
    );

    return FMath::Max(
        1,
        FMath::DivideAndRoundUp(
            OrderedAssets.Num(),
            SafeDisplayCount
        )
    );
}


/**
 * Alinea el desplazamiento con el comienzo de una página válida.
 */
void ACryptoTerminalActor::ClampScrollOffset(int32 TotalAssets)
{
    const int32 SafeDisplayCount = FMath::Clamp(
        DisplaySlotCount,
        1,
        25
    );

    if (TotalAssets <= 0)
    {
        GlobalScrollOffset = 0;
        return;
    }

    const int32 TotalPages = FMath::DivideAndRoundUp(
        TotalAssets,
        SafeDisplayCount
    );

    const int32 MaximumPageIndex = FMath::Max(
        0,
        TotalPages - 1
    );

    const int32 CurrentPageIndex =
        GlobalScrollOffset / SafeDisplayCount;

    const int32 ClampedPageIndex = FMath::Clamp(
        CurrentPageIndex,
        0,
        MaximumPageIndex
    );

    GlobalScrollOffset = ClampedPageIndex * SafeDisplayCount;
}


// ============================================================
// DISTRIBUCIÓN ESPACIAL
// ============================================================

/**
 * Calcula la posición relativa de un slot según el modo seleccionado.
 */
FVector ACryptoTerminalActor::CalculateSlotPosition(
    int32 Index,
    int32 TotalElements
) const
{
    const int32 SafeTotalElements = FMath::Max(TotalElements, 1);
    const float SafeSpacing = FMath::Max(SlotSpacing, 1.0f);
    const float SafeRadius = FMath::Max(CircularLayoutRadius, 1.0f);

    FVector Offset = FVector::ZeroVector;

    switch (CurrentLayoutMode)
    {
    case ETerminalMode::Mode3D_Horizontal:
    {
        const float CenterOffset =
            Index - (SafeTotalElements - 1) / 2.0f;

        Offset = FVector(
            0.0f,
            CenterOffset * SafeSpacing,
            0.0f
        );

        break;
    }

    case ETerminalMode::Mode3D_Vertical:
    {
        const float CenterOffset =
            (SafeTotalElements - 1) / 2.0f - Index;

        Offset = FVector(
            0.0f,
            0.0f,
            CenterOffset * SafeSpacing
        );

        break;
    }

    case ETerminalMode::Mode3D_Diagonal:
    {
        const float CenterOffset =
            Index - (SafeTotalElements - 1) / 2.0f;

        Offset = FVector(
            0.0f,
            CenterOffset * SafeSpacing,
            -CenterOffset * SafeSpacing
        );

        break;
    }

    case ETerminalMode::Mode3D_Circular:
    {
        const float Angle =
            (2.0f * PI * Index) / SafeTotalElements;

        Offset = FVector(
            0.0f,
            FMath::Cos(Angle) * SafeRadius,
            FMath::Sin(Angle) * SafeRadius
        );

        break;
    }

    default:
    {
        break;
    }
    }

    return Offset;
}


// ============================================================
// CREACIÓN DE SLOTS
// ============================================================

/**
 * Crea una cantidad fija de actores visuales según DisplaySlotCount.
 *
 * Los slots se reutilizan durante las actualizaciones financieras.
 */
void ACryptoTerminalActor::CreateSlotActors()
{
    if (!SpawnedSlots.IsEmpty())
    {
        return;
    }

    if (!GetWorld())
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage] Terminal: no existe un World válido para crear slots.")
        );

        return;
    }

    if (!BaseSlotClass)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Vantage] Terminal: BaseSlotClass no está configurada.")
        );

        return;
    }

    const int32 SafeDisplayCount = FMath::Clamp(
        DisplaySlotCount,
        1,
        25
    );

    SpawnedSlots.Reserve(SafeDisplayCount);

    for (int32 Index = 0; Index < SafeDisplayCount; ++Index)
    {
        FActorSpawnParameters SpawnParameters;

        SpawnParameters.Owner = this;

        SpawnParameters.SpawnCollisionHandlingOverride =
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        ACryptoSlotActor* NewSlot = GetWorld()->SpawnActor<ACryptoSlotActor>(
            BaseSlotClass,
            GetActorTransform(),
            SpawnParameters
        );

        if (!NewSlot)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[Vantage] Terminal: no se pudo crear el slot %d."),
                Index
            );

            continue;
        }

        NewSlot->AttachToActor(
            this,
            FAttachmentTransformRules::KeepWorldTransform
        );

        NewSlot->SetActorRelativeLocation(
            CalculateSlotPosition(Index, SafeDisplayCount)
        );

        NewSlot->SetActorRelativeRotation(FRotator::ZeroRotator);

        SpawnedSlots.Add(NewSlot);
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[Vantage] Terminal: %d de %d slots creados."),
        SpawnedSlots.Num(),
        SafeDisplayCount
    );
}


// ============================================================
// RESOLUCIÓN DE PRESENTACIÓN: KEY + TEXTURA
// ============================================================

/**
 * Resuelve la KEY y la textura asociadas a un símbolo financiero.
 *
 * Primero busca coincidencia exacta y después intenta resolver
 * el símbolo base eliminando el sufijo USDT.
 */
bool ACryptoTerminalActor::ResolveAssetPresentation(
    const FString& AssetSymbol,
    FString& OutAssetKey,
    UTexture2D*& OutIconTexture
) const
{
    OutAssetKey.Empty();
    OutIconTexture = nullptr;

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Vantage][Terminal][TRACE] ResolveAssetPresentation ENTER | Symbol: '%s' | Map entries: %d"),
        *AssetSymbol,
        CoinTextureMap.Num()
    );

    // --------------------------------------------------------
    // 1. NORMALIZAR EL SÍMBOLO
    // --------------------------------------------------------

    const FString NormalizedSymbol =
        AssetSymbol.ToUpper().TrimStartAndEnd();

    if (NormalizedSymbol.IsEmpty())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Vantage][Terminal][TRACE] Resolución cancelada: símbolo vacío.")
        );

        return false;
    }

    // --------------------------------------------------------
    // 2. BUSCAR COINCIDENCIA EXACTA
    // --------------------------------------------------------

    for (const TPair<FString, TObjectPtr<UTexture2D>>& Entry : CoinTextureMap)
    {
        const FString NormalizedKey =
            Entry.Key.ToUpper().TrimStartAndEnd();

        if (NormalizedKey == NormalizedSymbol)
        {
            OutAssetKey = NormalizedKey;
            OutIconTexture = Entry.Value.Get();

            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[Vantage][Terminal][TRACE] MATCH EXACTO | Symbol: '%s' | AssetKey: '%s' | Texture: %s | TextureValid: %s"),
                *NormalizedSymbol,
                *OutAssetKey,
                *GetNameSafe(OutIconTexture),
                IsValid(OutIconTexture) ? TEXT("SI") : TEXT("NO")
            );

            return true;
        }
    }

    // --------------------------------------------------------
    // 3. COMPROBAR SUFIJO USDT
    // --------------------------------------------------------

    const FString MarketSuffix = TEXT("USDT");

    if (!NormalizedSymbol.EndsWith(
        MarketSuffix,
        ESearchCase::CaseSensitive
    ))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Vantage][Terminal][TRACE] SIN MATCH | '%s' no termina en USDT y no existe clave exacta."),
            *NormalizedSymbol
        );

        return false;
    }

    if (NormalizedSymbol.Len() <= MarketSuffix.Len())
    {
        return false;
    }

    // --------------------------------------------------------
    // 4. OBTENER SÍMBOLO BASE
    // --------------------------------------------------------

    const FString BaseSymbol = NormalizedSymbol.LeftChop(
        MarketSuffix.Len()
    );

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Vantage][Terminal][TRACE] Búsqueda por símbolo base | Original: '%s' | Base: '%s'"),
        *NormalizedSymbol,
        *BaseSymbol
    );

    // --------------------------------------------------------
    // 5. BUSCAR CLAVE BASE
    // --------------------------------------------------------

    for (const TPair<FString, TObjectPtr<UTexture2D>>& Entry : CoinTextureMap)
    {
        const FString NormalizedKey =
            Entry.Key.ToUpper().TrimStartAndEnd();

        if (NormalizedKey == BaseSymbol)
        {
            OutAssetKey = NormalizedKey;
            OutIconTexture = Entry.Value.Get();

            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[Vantage][Terminal][TRACE] MATCH BASE | Original: '%s' | AssetKey: '%s' | Texture: %s | TextureValid: %s"),
                *NormalizedSymbol,
                *OutAssetKey,
                *GetNameSafe(OutIconTexture),
                IsValid(OutIconTexture) ? TEXT("SI") : TEXT("NO")
            );

            return true;
        }
    }

    // --------------------------------------------------------
    // 6. NO SE ENCONTRÓ UNA CLAVE COMPATIBLE
    // --------------------------------------------------------

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Vantage][Terminal][TRACE] SIN PRESENTACIÓN | No existe una clave compatible para '%s'."),
        *NormalizedSymbol
    );

    return false;
}


// ============================================================
// ACTUALIZACIÓN DE SLOTS
// ============================================================

/**
 * Obtiene el catálogo ordenado del Subsystem y actualiza cada slot.
 *
 * Cada slot recibe:
 * - AssetData.
 * - IconTexture.
 * - AssetKey.
 */
void ACryptoTerminalActor::UpdateVisibleSlots()
{
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Vantage][Terminal][TRACE] UpdateVisibleSlots ENTER | Actor: %s | Slots: %d | Map entries: %d | Scroll: %d"),
        *GetName(),
        SpawnedSlots.Num(),
        CoinTextureMap.Num(),
        GlobalScrollOffset
    );

    if (!FinanceSubsystem)
    {
        FinanceSubsystem = ResolveFinanceSubsystem();
    }

    if (!FinanceSubsystem)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage][Terminal][TRACE] UpdateVisibleSlots ABORT | FinanceSubsystem inválido.")
        );

        RefreshNavigationWidget();

        return;
    }

    if (SpawnedSlots.IsEmpty())
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage][Terminal][TRACE] UpdateVisibleSlots ABORT | SpawnedSlots está vacío.")
        );

        RefreshNavigationWidget();

        return;
    }

    TArray<FCryptoAssetData> OrderedAssets;

    FinanceSubsystem->GetOrderedTargetAssets(OrderedAssets);

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Vantage][Terminal][TRACE] Catálogo obtenido | OrderedAssets: %d | FinanceSubsystem: %s"),
        OrderedAssets.Num(),
        *GetNameSafe(FinanceSubsystem)
    );

    if (OrderedAssets.IsEmpty())
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage][Terminal][TRACE] UpdateVisibleSlots ABORT | OrderedAssets está vacío.")
        );

        GlobalScrollOffset = 0;

        RefreshNavigationWidget();

        return;
    }

    // Validar y alinear el desplazamiento.
    ClampScrollOffset(OrderedAssets.Num());

    // --------------------------------------------------------
    // ACTUALIZAR CADA SLOT VISIBLE
    // --------------------------------------------------------

    for (int32 SlotIndex = 0; SlotIndex < SpawnedSlots.Num(); ++SlotIndex)
    {
        ACryptoSlotActor* Slot = SpawnedSlots[SlotIndex];

        if (!IsValid(Slot))
        {
            continue;
        }

        const int32 AssetIndex = GlobalScrollOffset + SlotIndex;

        // ----------------------------------------------------
        // NO HAY MÁS ACTIVOS PARA ESTA POSICIÓN
        // ----------------------------------------------------

        if (!OrderedAssets.IsValidIndex(AssetIndex))
        {
            FCryptoAssetData EmptyAsset;

            Slot->InitSlot(
                EmptyAsset,
                nullptr,
                FString()
            );

            Slot->SetActorHiddenInGame(true);
            Slot->SetActorEnableCollision(false);

            continue;
        }

        // ----------------------------------------------------
        // OBTENER DATOS DEL ACTIVO
        // ----------------------------------------------------

        const FCryptoAssetData& AssetData = OrderedAssets[AssetIndex];

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Vantage][Terminal][TRACE] Slot %d | AssetIndex: %d | Symbol: '%s' | SlotActor: %s"),
            SlotIndex,
            AssetIndex,
            *AssetData.Symbol,
            *GetNameSafe(Slot)
        );

        // ----------------------------------------------------
        // RESOLVER KEY Y TEXTURA
        // ----------------------------------------------------

        FString AssetKey;
        UTexture2D* IconTexture = nullptr;

        const bool bFoundPresentation = ResolveAssetPresentation(
            AssetData.Symbol,
            AssetKey,
            IconTexture
        );

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Vantage][Terminal][TRACE] Presentación resuelta | Slot: %d | Symbol: '%s' | AssetKey: '%s' | Texture: %s | KeyFound: %s | TextureValid: %s"),
            SlotIndex,
            *AssetData.Symbol,
            *AssetKey,
            *GetNameSafe(IconTexture),
            bFoundPresentation ? TEXT("SI") : TEXT("NO"),
            IsValid(IconTexture) ? TEXT("SI") : TEXT("NO")
        );

        // ----------------------------------------------------
        // ACTUALIZAR DATOS, KEY Y TEXTURA
        // ----------------------------------------------------

        Slot->SetActorHiddenInGame(false);
        Slot->SetActorEnableCollision(true);

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Vantage][Terminal][TRACE] Enviando InitSlot | Slot: %d | Symbol: '%s' | AssetKey: '%s' | Texture: %s"),
            SlotIndex,
            *AssetData.Symbol,
            *AssetKey,
            *GetNameSafe(IconTexture)
        );

        Slot->InitSlot(
            AssetData,
            IconTexture,
            AssetKey
        );

        // ----------------------------------------------------
        // ACTUALIZAR POSICIÓN
        // ----------------------------------------------------

        Slot->SetActorRelativeLocation(
            CalculateSlotPosition(
                SlotIndex,
                SpawnedSlots.Num()
            )
        );

        Slot->SetActorRelativeRotation(FRotator::ZeroRotator);
    }

    LastAppliedScrollOffset = GlobalScrollOffset;

    // --------------------------------------------------------
    // ACTUALIZAR CONTADOR DE NAVEGACIÓN
    // --------------------------------------------------------

    RefreshNavigationWidget();
}


// ============================================================
// ACTUALIZACIÓN PÚBLICA DEL TERMINAL
// ============================================================

/**
 * Punto de entrada público para solicitar una actualización visual.
 */
void ACryptoTerminalActor::RefreshTerminal()
{
    if (!FinanceSubsystem)
    {
        FinanceSubsystem = ResolveFinanceSubsystem();
    }

    if (!FinanceSubsystem)
    {
        RefreshNavigationWidget();
        return;
    }

    UpdateVisibleSlots();
}


// ============================================================
// EVENTO DE DATOS FINANCIEROS
// ============================================================

/**
 * Recibe la notificación del Subsystem cuando hay datos nuevos.
 *
 * El terminal vuelve a consultar el catálogo ordenado para mantener
 * la paginación estable.
 */
void ACryptoTerminalActor::HandleCryptoDataUpdated(
    const TArray<FCryptoAssetData>& UpdatedAssets
)
{
    UpdateVisibleSlots();
}


// ============================================================
// INTERACCIÓN CON ACTIVOS
// ============================================================

/**
 * Busca el activo directamente en el Subsystem mediante su símbolo.
 */
void ACryptoTerminalActor::HandleSlotTap(const FString& AssetSymbol)
{
    const FString NormalizedSymbol =
        AssetSymbol.ToUpper().TrimStartAndEnd();

    if (NormalizedSymbol.IsEmpty())
    {
        return;
    }

    if (!FinanceSubsystem)
    {
        FinanceSubsystem = ResolveFinanceSubsystem();
    }

    if (!FinanceSubsystem)
    {
        return;
    }

    FCryptoAssetData AssetData;

    if (!FinanceSubsystem->GetAssetData(NormalizedSymbol, AssetData))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Vantage] Terminal: no hay datos disponibles para el activo %s."),
            *NormalizedSymbol
        );

        return;
    }

    OnCryptoAssetSelected.Broadcast(AssetData.Symbol);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[Vantage] Activo seleccionado: %s | Precio: $%.8f"),
        *AssetData.Symbol,
        AssetData.LastPrice
    );
}


// ============================================================
// DEPURACIÓN EN PANTALLA
// ============================================================

/**
 * Muestra información de los activos de la página actual.
 *
 * Es una función de consulta y no modifica el estado del terminal.
 */
void ACryptoTerminalActor::PrintVisibleAssets() const
{
    if (!bEnableDebugOutput || !GEngine || !FinanceSubsystem)
    {
        return;
    }

    TArray<FCryptoAssetData> OrderedAssets;

    FinanceSubsystem->GetOrderedTargetAssets(OrderedAssets);

    const int32 SafeDisplayCount = FMath::Clamp(
        DisplaySlotCount,
        1,
        25
    );

    for (int32 SlotIndex = 0; SlotIndex < SafeDisplayCount; ++SlotIndex)
    {
        const int32 AssetIndex = GlobalScrollOffset + SlotIndex;

        if (!OrderedAssets.IsValidIndex(AssetIndex))
        {
            continue;
        }

        const FCryptoAssetData& Data = OrderedAssets[AssetIndex];

        const FString DebugMessage = FString::Printf(
            TEXT(
                "=== [SLOT %d] ===\n"
                "Símbolo: %s | Precio: $%.8f | Volumen: %.2f"
            ),
            SlotIndex,
            *Data.Symbol,
            Data.LastPrice,
            Data.Volume
        );

        GEngine->AddOnScreenDebugMessage(
            static_cast<uint64>(10 + SlotIndex),
            FMath::Max(PrintInterval, 0.1f) + 0.1f,
            FColor::Cyan,
            DebugMessage
        );
    }
}


// ============================================================
// TICK
// ============================================================

/**
 * No consulta ni procesa datos financieros en cada frame.
 *
 * Solo realiza dos tareas:
 * - Detectar si cambió el desplazamiento global.
 * - Ejecutar la salida de depuración cuando corresponde.
 */
void ACryptoTerminalActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // --------------------------------------------------------
    // 1. DETECTAR CAMBIOS DE PÁGINA
    // --------------------------------------------------------

    if (GlobalScrollOffset != LastAppliedScrollOffset)
    {
        UpdateVisibleSlots();
    }

    // --------------------------------------------------------
    // 2. SALIDA DE DEPURACIÓN OPCIONAL
    // --------------------------------------------------------

    if (!bEnableDebugOutput)
    {
        return;
    }

    TimeSinceLastPrint += DeltaTime;

    const float SafePrintInterval = FMath::Max(
        PrintInterval,
        0.1f
    );

    if (TimeSinceLastPrint < SafePrintInterval)
    {
        return;
    }

    TimeSinceLastPrint = 0.0f;

    PrintVisibleAssets();
}
