# Vantage

**Vantage** is a real-time cryptocurrency market visualizer built with **Unreal Engine 5 and C++**.

The project connects to Binance through a WebSocket stream, filters a configurable set of USDT trading pairs, stores the latest market data in a centralized subsystem, and exposes that data to in-world terminal and slot actors.

> Vantage is a learning and portfolio project focused on Unreal Engine C++, real-time data integration, and modular visualization. It is not a trading platform or an exchange.

## Overview

- **Engine:** Unreal Engine 5
- **Language:** C++
- **Market data source:** Binance WebSocket
- **Connection type:** All-market mini-ticker stream
- **Core architecture:** UGameInstanceSubsystem
- **Visualization:** Unreal actors, dynamic materials, and widget components

## Current Features

- Receives Binance mini-ticker updates through a WebSocket connection.
- Filters incoming market data using a configurable list of target symbols.
- Maintains a centralized cache of the latest data for each asset.
- Uses a thread-safe critical section when accessing shared market data.
- Broadcasts data updates at a controlled interval using FTSTicker.
- Provides asset lookup by symbol through the subsystem.
- Supports ordered retrieval of configured assets.
- Displays assets through a terminal actor with pagination.
- Uses slot actors, dynamic material instances, and widget components for the visual layer.

## Architecture

The system is divided into three main areas.

### 1. Market Data Layer

**UCryptoFinanceSubsystem**

A UGameInstanceSubsystem responsible for the market-data lifecycle.

Its responsibilities include:

- Opening and managing the Binance WebSocket connection.
- Parsing incoming JSON messages.
- Filtering symbols against the configured target list.
- Updating the in-memory asset cache.
- Providing access to cached asset data.
- Broadcasting updates at a controlled interval.

Current stream endpoint:

```text
wss://stream.binance.com:9443/ws/!miniTicker@arr
```

### 2. Interaction Layer

**CryptoInteractableInterface**

Defines a common interaction contract for objects that can respond to user interaction in the environment.

### 3. Visualization Layer

**ACryptoTerminalActor**

Acts as the central terminal for displaying market assets. It retrieves data from the finance subsystem and handles the current paginated asset view.

**ACryptoSlotActor**

Represents an individual visual slot. It uses a dynamic material instance and a widget component to display asset-related visual information.

## Data Flow

```text
Binance WebSocket
       |
       v
JSON message parsing
       |
       v
Target-symbol filtering
       |
       v
UCryptoFinanceSubsystem
       |
       v
Thread-safe asset cache
       |
       v
Controlled update broadcast
       |
       +--------------------+
       |                    |
       v                    v
ACryptoTerminalActor   ACryptoSlotActor
       |                    |
       v                    v
Asset pagination      3D slot visualization
```

## Source Structure

```text
Source/
└── Vantage/
    ├── Public/
    │   ├── ACryptoSlotActor.h
    │   ├── ACryptoTerminalActor.h
    │   ├── CryptoFinanceSubsystem.h
    │   └── CryptoInteractableInterface.h
    │
    ├── Private/
    │   ├── ACryptoSlotActor.cpp
    │   ├── ACryptoTerminalActor.cpp
    │   ├── CryptoFinanceSubsystem.cpp
    │   └── CryptoInteractableInterface.cpp
    │
    └── Vantage.Build.cs
```

## Technical Concepts

This project is being developed to practice and demonstrate:

- Unreal Engine C++ development.
- Game Instance Subsystems and lifecycle management.
- WebSocket communication with an external data provider.
- JSON parsing and data conversion.
- Data caching with TMap and symbol filtering with TSet.
- Thread-safe access to shared data using FCriticalSection.
- Periodic update scheduling with FTSTicker.
- Delegates and communication between gameplay systems.
- Dynamic Material Instances and actor-based visualization.
- Separation between data acquisition, interaction, and presentation.

## Current Development Status

The core data pipeline is implemented: the project connects to Binance, parses mini-ticker messages, filters configured assets, updates the cache, and exposes the data to the terminal system.

The visualization and interaction layer is still being developed. Some presentation details, including the complete asset information displayed by each slot, remain to be implemented.

### Planned Improvements

- Replace the all-market mini-ticker stream with individual streams for the configured symbols, reducing unnecessary incoming data.
- Add robust WebSocket reconnection and connection-state handling.
- Review callback and subsystem shutdown lifetimes.
- Complete the slot's asset-information presentation.
- Make terminal pagination use a deterministic asset order.
- Improve the visual layouts for different asset arrangements.
- Add clearer loading, disconnected, and data-unavailable states.
- Expand documentation as the architecture evolves.

## Project Goal

The goal of Vantage is to build a polished real-time market visualization experience while developing practical skills in Unreal Engine C++, networking, data structures, and modular software architecture.

The project is actively evolving. Features listed under **Current Features** reflect the implemented code; items under **Planned Improvements** are not yet complete.
