
#include "ACryptoTerminalActor.h"

// Actores visuales.
#include "ACryptoSlotActor.h"

// Sistema financiero.
#include "CryptoFinanceSubsystem.h"

// Unreal Engine.
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"


// ============================================================
// ESTADO EST�TICO
// ============================================================

/*
 * El desplazamiento se comparte entre las instancias del terminal.
 * Se conserva el comportamiento de la implementaci�n anterior.
 */
int32 ACryptoTerminalActor::GlobalScrollOffset = 0;


// ============================================================
// CONSTRUCTOR
// ============================================================

/**
 * ACryptoTerminalActor
 *
 * Configura el ciclo de actualizaci�n del Actor.
 *
 * Tick se utiliza para:
 * - Detectar cambios en GlobalScrollOffset.
 * - Ejecutar la impresi�n de depuraci�n cuando est� habilitada.
 *
 * La actualizaci�n financiera normal se realiza mediante delegado,
 * no consultando el Subsystem en cada frame.
 */
ACryptoTerminalActor::ACryptoTerminalActor()
{
    PrimaryActorTick.bCanEverTick = true;
}


// ============================================================
// CICLO DE VIDA
// ============================================================

/**
 * BeginPlay
 *
 * Obtiene el Subsystem financiero, registra el evento de actualizaci�n
 * y crea los slots visuales.
 */
void ACryptoTerminalActor::BeginPlay()
{
    Super::BeginPlay();

    // --------------------------------------------------------
    // 1. OBTENER EL SUBSYSTEM FINANCIERO
    // --------------------------------------------------------

    FinanceSubsystem = ResolveFinanceSubsystem();

    if (!FinanceSubsystem)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage] Terminal: no se pudo obtener CryptoFinanceSubsystem.")
        );

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
    // 2. SUSCRIBIRSE A LAS ACTUALIZACIONES FINANCIERAS
    // --------------------------------------------------------

    /*
     * AddUniqueDynamic evita registrar dos veces el mismo objeto
     * como receptor del evento.
     */
    FinanceSubsystem->OnCryptoDataUpdated.AddUniqueDynamic(
        this,
        &ACryptoTerminalActor::HandleCryptoDataUpdated
    );

    // --------------------------------------------------------
    // 3. CREAR LOS SLOTS VISUALES
    // --------------------------------------------------------

    if (bSpawnSlotActors)
    {
        CreateSlotActors();
    }

    // --------------------------------------------------------
    // 4. APLICAR LA PRIMERA ACTUALIZACI�N
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
 * EndPlay
 *
 * Desregistra el delegado antes de destruir el Actor.
 *
 * Esto evita que el Subsystem intente notificar a una instancia
 * del terminal que ya no existe.
 */
void ACryptoTerminalActor::EndPlay(
    const EEndPlayReason::Type EndPlayReason
)
{
    if (FinanceSubsystem)
    {
        FinanceSubsystem->OnCryptoDataUpdated.RemoveDynamic(
            this,
            &ACryptoTerminalActor::HandleCryptoDataUpdated
        );
    }

    // Destruir expl�citamente los actores administrados por el terminal.
    for (ACryptoSlotActor* Slot : SpawnedSlots)
    {
        if (IsValid(Slot))
        {
            Slot->Destroy();
        }
    }

    SpawnedSlots.Empty();

    FinanceSubsystem = nullptr;

    Super::EndPlay(EndPlayReason);
}


// ============================================================
// OBTENCI�N DEL SUBSYSTEM
// ============================================================

/**
 * ResolveFinanceSubsystem
 *
 * Centraliza la obtenci�n del Subsystem financiero.
 * Devuelve nullptr cuando no existe un GameInstance v�lido.
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
// PAGINACI�N
// ============================================================

/**
 * SetGlobalScrollOffset
 *
 * Modifica el desplazamiento global.
 *
 * El l�mite inferior se valida aqu�. El l�mite superior depende
 * de la cantidad real de activos y se corrige en ClampScrollOffset.
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
 * ClampScrollOffset
 *
 * Evita que la p�gina comience despu�s del �ltimo activo disponible.
 *
 * Si hay menos activos que slots visibles, el desplazamiento vuelve a cero.
 */
void ACryptoTerminalActor::ClampScrollOffset(int32 TotalAssets)
{
    const int32 SafeDisplayCount = FMath::Max(DisplaySlotCount, 1);

    const int32 MaximumOffset = FMath::Max(
        0,
        TotalAssets - SafeDisplayCount
    );

    GlobalScrollOffset = FMath::Clamp(
        GlobalScrollOffset,
        0,
        MaximumOffset
    );
}


// ============================================================
// DISTRIBUCI�N ESPACIAL
// ============================================================

/**
 * CalculateSlotPosition
 *
 * Calcula la posici�n relativa de un slot seg�n el modo seleccionado.
 *
 * El resultado se utiliza como ubicaci�n relativa al Actor terminal.
 * SlotSpacing y CircularLayoutRadius se configuran desde el editor.
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
        // ----------------------------------------------------
        // DISTRIBUCI�N HORIZONTAL
        // ----------------------------------------------------

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

    // ----------------------------------------------------
    // DISTRIBUCI�N VERTICAL
    // ----------------------------------------------------

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

    // ----------------------------------------------------
    // DISTRIBUCI�N DIAGONAL
    // ----------------------------------------------------

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

    // ----------------------------------------------------
    // DISTRIBUCI�N CIRCULAR
    // ----------------------------------------------------

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
// CREACI�N DE SLOTS
// ============================================================

/**
 * CreateSlotActors
 *
 * Crea una cantidad fija de actores visuales seg�n DisplaySlotCount.
 *
 * Los slots se reutilizan durante las actualizaciones financieras:
 * no se destruyen ni se vuelven a crear cuando cambia el precio.
 */
void ACryptoTerminalActor::CreateSlotActors()
{
    // Evitar crear slots duplicados.
    if (!SpawnedSlots.IsEmpty())
    {
        return;
    }

    if (!GetWorld())
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage] Terminal: no existe un World v�lido para crear slots.")
        );

        return;
    }

    if (!BaseSlotClass)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT(
                "[Vantage] Terminal: BaseSlotClass no est� configurada. "
                "No se crear�n actores visuales."
            )
        );

        return;
    }

    const int32 SafeDisplayCount = FMath::Clamp(
        DisplaySlotCount,
        1,
        25
    );

    SpawnedSlots.Reserve(SafeDisplayCount);

    // --------------------------------------------------------
    // CREAR CADA ACTOR
    // --------------------------------------------------------

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

        // Adjuntar el slot al terminal para mantener una jerarqu�a clara.
        NewSlot->AttachToActor(
            this,
            FAttachmentTransformRules::KeepWorldTransform
        );

        // Aplicar la posici�n relativa inicial.
        NewSlot->SetActorRelativeLocation(
            CalculateSlotPosition(Index, SafeDisplayCount)
        );

        NewSlot->SetActorRelativeRotation(FRotator::ZeroRotator);

        SpawnedSlots.Add(NewSlot);
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT(
            "[Vantage] Terminal: %d de %d slots creados."
        ),
        SpawnedSlots.Num(),
        SafeDisplayCount
    );
}


// ============================================================
// B�SQUEDA DE TEXTURAS
// ============================================================

/**
 * FindIconTexture
 *
 * Busca la textura asociada a un s�mbolo de criptomoneda.
 *
 * COMPATIBILIDAD DE S�MBOLOS:
 *
 * El Subsystem puede recibir s�mbolos de mercado completos:
 * - BTCUSDT
 * - ETHUSDT
 * - BNBUSDT
 *
 * CoinTextureMap almacena claves individuales:
 * - BTC
 * - ETH
 * - BNB
 *
 * La b�squeda se realiza en dos etapas:
 *
 * 1. Se intenta una coincidencia exacta.
 * 2. Si no existe y el s�mbolo termina en USDT, se elimina
 *    ese sufijo y se intenta encontrar la clave base.
 *
 * La comparaci�n no distingue may�sculas y min�sculas.
 *
 * El s�mbolo financiero original no se modifica.
 *
 * @param AssetSymbol S�mbolo recibido desde el Subsystem.
 *
 * @return Textura correspondiente o nullptr si no existe coincidencia.
 */
UTexture2D* ACryptoTerminalActor::FindIconTexture(
    const FString& AssetSymbol
) const
{
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Vantage][Terminal][TRACE] FindIconTexture ENTER | Símbolo recibido: '%s' | Entradas del mapa: %d"),
        *AssetSymbol,
        CoinTextureMap.Num()
    );

    // --------------------------------------------------------
    // 1. NORMALIZAR EL S�MBOLO RECIBIDO
    // --------------------------------------------------------

    const FString NormalizedSymbol =
        AssetSymbol.ToUpper().TrimStartAndEnd();

    if (NormalizedSymbol.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[Vantage][Terminal][TRACE] FindIconTexture: símbolo vacío después de normalizar."));
        return nullptr;
    }

    // --------------------------------------------------------
    // 2. BUSCAR UNA COINCIDENCIA EXACTA
    // --------------------------------------------------------

    /*
     * La coincidencia exacta tiene prioridad.
     *
     * Esto permite agregar en el futuro claves como BTCUSDT
     * directamente al mapa sin cambiar esta funci�n.
     */
    for (const TPair<FString, TObjectPtr<UTexture2D>>& Entry : CoinTextureMap)
    {
        const FString NormalizedKey =
            Entry.Key.ToUpper().TrimStartAndEnd();

        UE_LOG(
            LogTemp,
            Verbose,
            TEXT("[Vantage][Terminal][TRACE] Comparación exacta | Símbolo: '%s' | Key: '%s' | Texture: %s"),
            *NormalizedSymbol,
            *NormalizedKey,
            *GetNameSafe(Entry.Value.Get())
        );

        if (NormalizedKey == NormalizedSymbol)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[Vantage][Terminal][TRACE] MATCH EXACTO | Símbolo: '%s' | Key: '%s' | Texture: %s | Valid: %s"),
                *NormalizedSymbol,
                *NormalizedKey,
                *GetNameSafe(Entry.Value.Get()),
                IsValid(Entry.Value.Get()) ? TEXT("SI") : TEXT("NO")
            );
            return Entry.Value.Get();
        }
    }

    // --------------------------------------------------------
    // 3. COMPROBAR EL SUFIJO USDT
    // --------------------------------------------------------

    const FString MarketSuffix = TEXT("USDT");

    if (!NormalizedSymbol.EndsWith(
        MarketSuffix,
        ESearchCase::CaseSensitive
    ))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage][Terminal][TRACE] SIN MATCH | '%s' no termina en USDT. No se encontró clave exacta."),
            *NormalizedSymbol
        );
        return nullptr;
    }

    /*
     * Evitar procesar un s�mbolo que est� compuesto solamente
     * por el sufijo de cotizaci�n.
     */
    if (NormalizedSymbol.Len() <= MarketSuffix.Len())
    {
        return nullptr;
    }

    // --------------------------------------------------------
    // 4. OBTENER EL S�MBOLO BASE
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
    // 5. BUSCAR LA CLAVE BASE EN EL MAPA
    // --------------------------------------------------------

    for (const TPair<FString, TObjectPtr<UTexture2D>>& Entry : CoinTextureMap)
    {
        const FString NormalizedKey =
            Entry.Key.ToUpper().TrimStartAndEnd();

        UE_LOG(
            LogTemp,
            Verbose,
            TEXT("[Vantage][Terminal][TRACE] Comparación base | Base: '%s' | Key: '%s' | Texture: %s"),
            *BaseSymbol,
            *NormalizedKey,
            *GetNameSafe(Entry.Value.Get())
        );

        if (NormalizedKey == BaseSymbol)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[Vantage][Terminal][TRACE] MATCH BASE | Símbolo: '%s' | Key: '%s' | Texture: %s | Valid: %s"),
                *NormalizedSymbol,
                *NormalizedKey,
                *GetNameSafe(Entry.Value.Get()),
                IsValid(Entry.Value.Get()) ? TEXT("SI") : TEXT("NO")
            );
            return Entry.Value.Get();
        }
    }

    // --------------------------------------------------------
    // 6. NO SE ENCONTR� NINGUNA TEXTURA
    // --------------------------------------------------------

    return nullptr;
}


// ============================================================
// ACTUALIZACI�N DE SLOTS
// ============================================================

/**
 * UpdateVisibleSlots
 *
 * Obtiene el cat�logo ordenado del Subsystem y actualiza cada slot.
 *
 * Se utiliza GetOrderedTargetAssets para mantener posiciones estables,
 * incluso cuando algunos activos todav�a no recibieron datos.
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
        UE_LOG(LogTemp, Error, TEXT("[Vantage][Terminal][TRACE] UpdateVisibleSlots ABORT | FinanceSubsystem inválido."));
        return;
    }

    if (SpawnedSlots.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[Vantage][Terminal][TRACE] UpdateVisibleSlots ABORT | SpawnedSlots está vacío."));
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
        UE_LOG(LogTemp, Error, TEXT("[Vantage][Terminal][TRACE] UpdateVisibleSlots ABORT | OrderedAssets está vacío."));
        return;
    }

    // Validar el desplazamiento antes de consultar los activos visibles.
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
        // NO HAY M�S ACTIVOS PARA ESTA POSICI�N
        // ----------------------------------------------------

        if (!OrderedAssets.IsValidIndex(AssetIndex))
        {
            FCryptoAssetData EmptyAsset;

            Slot->InitSlot(EmptyAsset, nullptr);

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

        /*
         * Ejemplo de resoluci�n:
         *
         * AssetData.Symbol = BTCUSDT
         * CoinTextureMap contiene BTC
         * FindIconTexture devuelve la textura de Bitcoin.
         */
        UTexture2D* IconTexture = FindIconTexture(AssetData.Symbol);

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Vantage][Terminal][TRACE] Resultado de búsqueda | Slot: %d | Symbol: '%s' | Texture: %s | Valid: %s"),
            SlotIndex,
            *AssetData.Symbol,
            *GetNameSafe(IconTexture),
            IsValid(IconTexture) ? TEXT("SI") : TEXT("NO")
        );

        // ----------------------------------------------------
        // ACTUALIZAR DATOS Y TEXTURA
        // ----------------------------------------------------

        Slot->SetActorHiddenInGame(false);
        Slot->SetActorEnableCollision(true);

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Vantage][Terminal][TRACE] Enviando datos a InitSlot | Slot: %d | Symbol: '%s' | Texture: %s"),
            SlotIndex,
            *AssetData.Symbol,
            *GetNameSafe(IconTexture)
        );

        Slot->InitSlot(
            AssetData,
            IconTexture
        );

        // ----------------------------------------------------
        // ACTUALIZAR POSICI�N
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
}


// ============================================================
// ACTUALIZACI�N P�BLICA DEL TERMINAL
// ============================================================

/**
 * RefreshTerminal
 *
 * Punto de entrada p�blico para solicitar una actualizaci�n visual.
 * Puede llamarse desde Blueprint despu�s de modificar la paginaci�n.
 */
void ACryptoTerminalActor::RefreshTerminal()
{
    if (!FinanceSubsystem)
    {
        FinanceSubsystem = ResolveFinanceSubsystem();
    }

    if (!FinanceSubsystem)
    {
        return;
    }

    UpdateVisibleSlots();
}


// ============================================================
// EVENTO DE DATOS FINANCIEROS
// ============================================================

/**
 * HandleCryptoDataUpdated
 *
 * Recibe la notificaci�n del Subsystem cuando hay datos nuevos.
 *
 * El array se recibe como referencia constante. El terminal no necesita
 * copiarlo porque vuelve a consultar el cat�logo ordenado para obtener
 * los placeholders y mantener la paginaci�n estable.
 */
void ACryptoTerminalActor::HandleCryptoDataUpdated(
    const TArray<FCryptoAssetData>& UpdatedAssets
)
{
    UpdateVisibleSlots();
}


// ============================================================
// INTERACCI�N CON ACTIVOS
// ============================================================

/**
 * HandleSlotTap
 *
 * Busca el activo directamente en el Subsystem mediante su s�mbolo.
 * Evita recorrer todo el array de activos para encontrar una coincidencia.
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
            TEXT(
                "[Vantage] Terminal: no hay datos disponibles "
                "para el activo %s."
            ),
            *NormalizedSymbol
        );

        return;
    }

    // Notificar a Blueprint qu� activo fue seleccionado.
    OnCryptoAssetSelected.Broadcast(AssetData.Symbol);

    UE_LOG(
        LogTemp,
        Log,
        TEXT(
            "[Vantage] Activo seleccionado: %s | Precio: $%.8f"
        ),
        *AssetData.Symbol,
        AssetData.LastPrice
    );
}


// ============================================================
// DEPURACI�N EN PANTALLA
// ============================================================

/**
 * PrintVisibleAssets
 *
 * Muestra informaci�n de los activos de la p�gina actual.
 *
 * Es una funci�n de consulta: no modifica el estado del terminal.
 * Por eso se declara y define como const.
 */
void ACryptoTerminalActor::PrintVisibleAssets() const
{
    if (!bEnableDebugOutput || !GEngine || !FinanceSubsystem)
    {
        return;
    }

    TArray<FCryptoAssetData> OrderedAssets;

    FinanceSubsystem->GetOrderedTargetAssets(OrderedAssets);

    for (int32 SlotIndex = 0; SlotIndex < DisplaySlotCount; ++SlotIndex)
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
                "S�mbolo: %s | Precio: $%.8f | Volumen: %.2f"
            ),
            SlotIndex,
            *Data.Symbol,
            Data.LastPrice,
            Data.Volume
        );

        /*
         * AddOnScreenDebugMessage recibe un identificador uint64,
         * un tiempo de visualizaci�n, un color y el mensaje.
         *
         * El identificador permite reutilizar la misma l�nea
         * de depuraci�n para cada slot.
         */
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
 * Tick
 *
 * No consulta ni procesa datos financieros en cada frame.
 *
 * Solo realiza dos tareas:
 * - Detectar si cambi� el desplazamiento global.
 * - Ejecutar la salida de depuraci�n cuando corresponde.
 */
void ACryptoTerminalActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // --------------------------------------------------------
    // 1. DETECTAR CAMBIOS DE P�GINA
    // --------------------------------------------------------

    if (GlobalScrollOffset != LastAppliedScrollOffset)
    {
        UpdateVisibleSlots();
    }

    // --------------------------------------------------------
    // 2. SALIDA DE DEPURACI�N OPCIONAL
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