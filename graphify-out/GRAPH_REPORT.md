# Graph Report - .  (2026-07-02)

## Corpus Check
- Corpus is ~18,392 words - fits in a single context window. You may not need a graph.

## Summary
- 78 nodes · 114 edges · 9 communities (8 shown, 1 thin omitted)
- Extraction: 96% EXTRACTED · 4% INFERRED · 0% AMBIGUOUS · INFERRED: 5 edges (avg confidence: 0.93)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Community 0|Community 0]]
- [[_COMMUNITY_Community 1|Community 1]]
- [[_COMMUNITY_Community 2|Community 2]]
- [[_COMMUNITY_Community 3|Community 3]]
- [[_COMMUNITY_Community 4|Community 4]]
- [[_COMMUNITY_Community 5|Community 5]]
- [[_COMMUNITY_Community 6|Community 6]]
- [[_COMMUNITY_Community 7|Community 7]]
- [[_COMMUNITY_Community 8|Community 8]]

## God Nodes (most connected - your core abstractions)
1. `AppState` - 22 edges
2. `Smart Mirror Project` - 13 edges
3. `loop()` - 9 edges
4. `PCB Layout Render (Job1)` - 8 edges
5. `processAnimation()` - 7 edges
6. `handleJsonCommand()` - 5 edges
7. `handleSimpleCommand()` - 5 edges
8. `triggerPowerOn()` - 5 edges
9. `setGlobalColor()` - 5 edges
10. `NetworkTaskCode()` - 4 edges

## Surprising Connections (you probably didn't know these)
- `ESP32 Module Footprint` --semantically_similar_to--> `ESP32-S3 Zero MCU`  [INFERRED] [semantically similar]
  Altium/Job1.jpg → README.md
- `LED102 Connector (PA LED102 0-2)` --semantically_similar_to--> `SK6812 RGBW LED Strip`  [INFERRED] [semantically similar]
  Altium/Job1.jpg → README.md
- `PIR Connector (PA PIR 0-3)` --semantically_similar_to--> `PIR Motion Sensor (HC-SR501 / AM312)`  [INFERRED] [semantically similar]
  Altium/Job1.jpg → README.md
- `SN74LVC2G14 Footprint (PA SN74LVC2G14 1-6)` --semantically_similar_to--> `SN74LVC2G14 Schmitt Trigger`  [INFERRED] [semantically similar]
  Altium/Job1.jpg → README.md
- `Button Connector (PA Button 0-2)` --semantically_similar_to--> `Hardware Button`  [INFERRED] [semantically similar]
  Altium/Job1.jpg → README.md

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Mirror Hardware Components** — readme_esp32_s3_zero, readme_sk6812_rgbw, readme_ws2812b_status, readme_pir_sensor, readme_hardware_button, readme_sn74lvc2g14, altium_job1_pcb_module, altium_job1_pcb_sn74, altium_job1_pcb_pir, altium_job1_pcb_led, altium_job1_pcb_button [EXTRACTED 1.00]
- **Dual-Core Synchronization Pattern** — readme_two_core_arch, readme_mutex_safety, readme_show_outside_mutex, readme_appstate, readme_cfg_volatile [EXTRACTED 1.00]

## Communities (9 total, 1 thin omitted)

### Community 0 - "Community 0"
Cohesion: 0.09
Nodes (22): AppState, b, currentMode, forceUpdate, g, isMakeup, isRainbowSnake, lastAnimTimer (+14 more)

### Community 1 - "Community 1"
Cohesion: 0.18
Nodes (14): LED102 Connector (PA LED102 0-2), Home Assistant, Makeup Mode (white W channel), HA MQTT Discovery, MQTT Interface (JSON Light), PlatformIO Build System, Color/Brightness Reset on Off, secrets.h (WiFi/MQTT credentials) (+6 more)

### Community 2 - "Community 2"
Cohesion: 0.18
Nodes (12): PCB Layout Render (Job1), Button Connector (PA Button 0-2), PIR Connector (PA PIR 0-3), Power Connector (PA Power 0-2), Resistor Arrays (R1-R4, CO R1-R4), SN74LVC2G14 Footprint (PA SN74LVC2G14 1-6), Altium Production Output - Job1, 15-Minute Auto-Off Timer (+4 more)

### Community 3 - "Community 3"
Cohesion: 0.54
Nodes (7): applyDefaults(), getDarkFactor(), packColor(), processAnimation(), scale8(), setGlobalColor(), setVirtualPixel()

### Community 4 - "Community 4"
Cohesion: 0.29
Nodes (7): ESP32 Module Footprint, AppState Shared Structure, Cfg (volatile configuration), ESP32-S3 Zero MCU, Thread Safety (stateMutex, volatile Cfg), show() Outside Mutex Optimization, Dual-Core Architecture (FreeRTOS)

### Community 5 - "Community 5"
Cohesion: 0.60
Nodes (5): byte, handleJsonCommand(), handleSimpleCommand(), mqttCallback(), triggerPowerOff()

### Community 6 - "Community 6"
Cohesion: 0.67
Nodes (4): loop(), resetActivityTimer(), showStrips(), triggerPowerOn()

### Community 7 - "Community 7"
Cohesion: 0.50
Nodes (4): NetworkTaskCode(), publishDiscovery(), setStatusLED(), setup()

## Knowledge Gaps
- **30 isolated node(s):** `currentMode`, `r`, `g`, `b`, `isMakeup` (+25 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `AppState` connect `Community 0` to `Community 3`?**
  _High betweenness centrality (0.237) - this node is a cross-community bridge._
- **Why does `Smart Mirror Project` connect `Community 1` to `Community 2`, `Community 4`?**
  _High betweenness centrality (0.109) - this node is a cross-community bridge._
- **Why does `PCB Layout Render (Job1)` connect `Community 2` to `Community 1`, `Community 4`?**
  _High betweenness centrality (0.044) - this node is a cross-community bridge._
- **What connects `currentMode`, `r`, `g` to the rest of the system?**
  _33 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Community 0` be split into smaller, more focused modules?**
  _Cohesion score 0.09090909090909091 - nodes in this community are weakly interconnected._