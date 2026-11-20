// ============================================================================
// MorbiEngine - Zero-Parse Binary IPC & Live JSON Bridge (nativeBridge.ts)
// ============================================================================
// Directly mirrors C++ `Morbi::UI::BinaryIPCBridge` and `WebView2Compositor`.
// Supports high-performance 64-byte Binary IPC packets for viewport gizmo drag,
// as well as JSON streaming for Scene Hierarchy, Asset Registry, and Console Logs.
// ============================================================================

import { HierarchyItem, AssetItem, ConsoleLogMessage, GizmoTool } from '../types';

export const BINARY_IPC_MAGIC = 0x4d554942; // 'MUIB'
export const BINARY_IPC_VERSION = 1;
export const PACKET_HEADER_BYTES = 16;

export enum BinaryIPCOpcode {
  Nop = 0,
  SyncEntityTransform = 1,     // 48-byte payload (EntityId u64 + Pos[3] + RotQuat[4] + Scale[3])
  ViewportResize = 2,          // 24-byte payload (Width, Height, DpiScale, SharedTextureHandle)
  CameraOrbitInput = 3,        // 16-byte payload (DeltaYaw, DeltaPitch, DeltaZoom, Buttons)
  EngineTelemetrySnapshot = 4, // 32-byte payload (FrameTimeMs, Fps, Entities, Physics, HotReload, Passes)
  SelectEntity = 5,            // 8-byte payload  (PackedEntityId)
  ExecuteCommand = 6,          // UTF-8 command payload
  ViewportPick = 7             // 8-byte payload (NormalizedX float, NormalizedY float)
}

export interface EntityTransformState {
  packedEntityId: bigint;
  position: [number, number, number];
  rotationQuat: [number, number, number, number];
  scale: [number, number, number];
}

export interface EngineTelemetryState {
  frameTimeMs: number;
  fps: number;
  aliveEntities: number;
  physicsActiveBodies: number;
  physicsSleepingBodies?: number;
  physicsDynamicBodies?: number;
  physicsKinematicBodies?: number;
  physicsStaticBodies?: number;
  physicsTotalBodies?: number;
  physicsJoints?: number;
  physicsCharacters?: number;
  physicsCollisions?: number;
  physicsTriggers?: number;
  physicsSubsteps?: number;
  physicsQueries?: number;
  physicsSimMs?: number;
  physicsDebugDraw?: boolean;
  hotReloadGeneration: number;
  renderGraphPasses: number;
  aliasedTexturesReused: number;
  audioVoicesPlayed: number;
}

export interface SharedViewportState {
  width: number;
  height: number;
  dpiScale: number;
  sharedTextureHandle: bigint;
}

export interface MenuRect {
  x: number;
  y: number;
  width: number;
  height: number;
}

declare global {
  interface Window {
    chrome?: {
      webview?: {
        postMessage: (message: ArrayBuffer | string) => void;
        addEventListener: (
          type: 'message',
          listener: (event: { data: ArrayBuffer | string | Record<string, unknown> }) => void
        ) => void;
      };
    };
  }
}

export class MorbiNativeBridge {
  private sequenceId = 1;
  private activeOverlays = new Map<string, MenuRect>();
  private transformListeners = new Set<(state: EntityTransformState) => void>();
  private telemetryListeners = new Set<(state: EngineTelemetryState) => void>();
  private viewportListeners = new Set<(state: SharedViewportState) => void>();
  private selectionListeners = new Set<(packedEntityId: bigint) => void>();

  // Live JSON event listeners
  private sceneSyncListeners = new Set<(sceneName: string, entities: HierarchyItem[]) => void>();
  private assetSyncListeners = new Set<(folders: string[], assets: AssetItem[]) => void>();
  private logEntryListeners = new Set<(log: ConsoleLogMessage) => void>();
  private liveTransformListeners = new Set<(entityId: number, pos: [number, number, number], rot: [number, number, number], scale: [number, number, number]) => void>();
  private playModeListeners = new Set<(mode: 'edit' | 'playing' | 'paused') => void>();

  constructor() {
    if (typeof window !== 'undefined' && window.chrome?.webview) {
      window.chrome.webview.addEventListener('message', (event) => {
        if (event.data instanceof ArrayBuffer) {
          this.decodeBinaryBatch(event.data);
          return;
        }

        // Handle JSON payloads from WebView2
        let payload: unknown = event.data;
        if (typeof payload === 'string') {
          try {
            payload = JSON.parse(payload);
          } catch {
            return;
          }
        }

        if (payload && typeof payload === 'object') {
          this.handleJsonMessage(payload as Record<string, unknown>);
        }
      });
    }
  }

  private handleJsonMessage(msg: Record<string, unknown>): void {
    const type = typeof msg.type === 'string' ? msg.type : '';
    if (type === 'scene_sync') {
      const sceneName = typeof msg.sceneName === 'string' && msg.sceneName.length > 0 ? msg.sceneName : 'DefaultScene';
      const entities: HierarchyItem[] = Array.isArray(msg.entities) ? (msg.entities as HierarchyItem[]) : [];
      this.sceneSyncListeners.forEach((cb) => cb(sceneName, entities));
    } else if (type === 'assets_sync') {
      const folders: string[] = Array.isArray(msg.folders) ? (msg.folders as string[]) : [];
      const assets: AssetItem[] = Array.isArray(msg.assets) ? (msg.assets as AssetItem[]) : [];
      this.assetSyncListeners.forEach((cb) => cb(folders, assets));
    } else if (type === 'log_entry') {
      const rawLevel = String(msg.level || 'info').toLowerCase();
      let level: 'info' | 'warning' | 'error' | 'debug' = 'info';
      if (rawLevel.includes('warn')) level = 'warning';
      else if (rawLevel.includes('err')) level = 'error';
      else if (rawLevel.includes('debug') || rawLevel.includes('trace')) level = 'debug';

      const category = typeof msg.category === 'string' && msg.category.length > 0 ? msg.category : 'Engine';
      const message = typeof msg.message === 'string' ? msg.message : '';
      const timestamp = typeof msg.timestamp === 'string' && msg.timestamp.length > 0 ? msg.timestamp : new Date().toLocaleTimeString();

      const entry: ConsoleLogMessage = {
        id: String(Date.now()) + Math.random().toString(36).substring(2, 6),
        level,
        category,
        message: `[${category}] ${message}`,
        timestamp
      };
      this.logEntryListeners.forEach((cb) => cb(entry));
    } else if (type === 'telemetry_sync') {
      const telem: EngineTelemetryState = {
        frameTimeMs: Number(msg.frameTimeMs) || 16.6,
        fps: Number(msg.fps) || 60.0,
        aliveEntities: Number(msg.aliveEntities) || 0,
        physicsActiveBodies: Number(msg.physicsActiveBodies) || 0,
        physicsSleepingBodies: Number(msg.physicsSleepingBodies) || 0,
        physicsDynamicBodies: Number(msg.physicsDynamicBodies) || 0,
        physicsKinematicBodies: Number(msg.physicsKinematicBodies) || 0,
        physicsStaticBodies: Number(msg.physicsStaticBodies) || 0,
        physicsTotalBodies: Number(msg.physicsTotalBodies) || 0,
        physicsJoints: Number(msg.physicsJoints) || 0,
        physicsCharacters: Number(msg.physicsCharacters) || 0,
        physicsCollisions: Number(msg.physicsCollisions) || 0,
        physicsTriggers: Number(msg.physicsTriggers) || 0,
        physicsSubsteps: Number(msg.physicsSubsteps) || 0,
        physicsQueries: Number(msg.physicsQueries) || 0,
        physicsSimMs: Number(msg.physicsSimMs) || 0,
        physicsDebugDraw: Boolean(msg.physicsDebugDraw),
        hotReloadGeneration: Number(msg.hotReloadGeneration) || 0,
        renderGraphPasses: Number(msg.renderGraphPasses) || 0,
        aliasedTexturesReused: Number(msg.aliasedTexturesReused) || 0,
        audioVoicesPlayed: Number(msg.audioVoicesPlayed) || 0
      };
      this.telemetryListeners.forEach((cb) => cb(telem));
    } else if (type === 'transform_sync') {
      const entityId = Number(msg.entityId) || 0;
      const pos: [number, number, number] = Array.isArray(msg.position) ? [msg.position[0], msg.position[1], msg.position[2]] : [0, 0, 0];
      const rot: [number, number, number] = Array.isArray(msg.rotation) ? [msg.rotation[0], msg.rotation[1], msg.rotation[2]] : [0, 0, 0];
      const scale: [number, number, number] = Array.isArray(msg.scale) ? [msg.scale[0], msg.scale[1], msg.scale[2]] : [1, 1, 1];
      this.liveTransformListeners.forEach((cb) => cb(entityId, pos, rot, scale));
    } else if (type === 'play_mode_sync') {
      const mode = (msg.playMode === 'playing' || msg.playMode === 'paused' || msg.playMode === 'edit') ? msg.playMode : 'edit';
      this.playModeListeners.forEach((cb) => cb(mode));
    } else if (type === 'select_entity_sync') {
      const entityId = Number(msg.entityId) || 0;
      this.selectionListeners.forEach((cb) => cb(BigInt(entityId)));
    }
  }

  // --- Subscriptions ---
  public onSceneSync(cb: (sceneName: string, entities: HierarchyItem[]) => void): () => void {
    this.sceneSyncListeners.add(cb);
    return () => this.sceneSyncListeners.delete(cb);
  }

  public onAssetsSync(cb: (folders: string[], assets: AssetItem[]) => void): () => void {
    this.assetSyncListeners.add(cb);
    return () => this.assetSyncListeners.delete(cb);
  }

  public onLogEntry(cb: (entry: ConsoleLogMessage) => void): () => void {
    this.logEntryListeners.add(cb);
    return () => this.logEntryListeners.delete(cb);
  }

  public onLiveTransform(cb: (entityId: number, pos: [number, number, number], rot: [number, number, number], scale: [number, number, number]) => void): () => void {
    this.liveTransformListeners.add(cb);
    return () => this.liveTransformListeners.delete(cb);
  }

  public onTransformSync(cb: (state: EntityTransformState) => void): () => void {
    this.transformListeners.add(cb);
    return () => this.transformListeners.delete(cb);
  }

  public onTelemetry(cb: (state: EngineTelemetryState) => void): () => void {
    this.telemetryListeners.add(cb);
    return () => this.telemetryListeners.delete(cb);
  }

  public onViewportSharedTexture(cb: (state: SharedViewportState) => void): () => void {
    this.viewportListeners.add(cb);
    return () => this.viewportListeners.delete(cb);
  }

  public onEntitySelected(cb: (packedEntityId: bigint) => void): () => void {
    this.selectionListeners.add(cb);
    return () => this.selectionListeners.delete(cb);
  }

  public onPlayModeChange(cb: (mode: 'edit' | 'playing' | 'paused') => void): () => void {
    this.playModeListeners.add(cb);
    return () => this.playModeListeners.delete(cb);
  }

  // --- Outbound Messages ---
  public postJson(obj: Record<string, unknown>): void {
    if (typeof window !== 'undefined' && window.chrome?.webview) {
      window.chrome.webview.postMessage(JSON.stringify(obj));
    }
  }

  public sendTransformUpdate(
    entityId: number,
    position: [number, number, number],
    rotation: [number, number, number],
    scale: [number, number, number]
  ): void {
    this.postJson({
      type: 'update_transform',
      entityId,
      position,
      rotation,
      scale
    });
  }

  public sendSelectEntityId(entityId: number): void {
    this.postJson({
      type: 'select_entity',
      entityId
    });
  }

  public updatePhysicsComponent(
    entityId: number,
    componentKind: 'rigidbody' | 'characterController' | 'compoundColliders' | 'physicsJoints',
    patch: Record<string, unknown>
  ): void {
    const normalizedPatch = { ...patch };
    if (
      componentKind === 'physicsJoints' &&
      typeof normalizedPatch.type === 'string' &&
      normalizedPatch.type !== 'update_physics_component' &&
      !normalizedPatch.jointType
    ) {
      normalizedPatch.jointType = normalizedPatch.type;
    }
    this.postJson({
      ...normalizedPatch,
      type: 'update_physics_component',
      entityId,
      componentKind
    });
  }

  public sendSetPhysicsDebugDraw(enabled: boolean): void {
    this.postJson({
      type: 'set_physics_debug_draw',
      enabled
    });
  }

  public sendPlay(): void {
    this.postJson({
      type: 'play'
    });
  }

  public sendPause(): void {
    this.postJson({
      type: 'pause'
    });
  }

  public sendStop(): void {
    this.postJson({
      type: 'stop'
    });
  }

  public sendPlayPause(paused: boolean): void {
    this.postJson({
      type: 'play_pause',
      paused
    });
  }

  public sendStep(): void {
    this.postJson({
      type: 'step'
    });
  }

  public sendInstantiateAsset(assetPath: string, assetName: string, normX: number, normY: number): void {
    this.postJson({
      type: 'instantiate_asset',
      assetPath,
      assetName,
      normX,
      normY
    });
  }

  public sendNewScene(): void {
    this.postJson({
      type: 'new_scene'
    });
  }

  public sendLoadScene(scenePath: string): void {
    this.postJson({
      type: 'load_scene',
      path: scenePath
    });
  }

  public sendOpenSceneDialog(): void {
    this.postJson({
      type: 'open_scene_dialog'
    });
  }

  public sendSaveScene(scenePath?: string): void {
    this.postJson({
      type: 'save_scene',
      path: scenePath || ''
    });
  }

  public sendSaveSceneDialog(): void {
    this.postJson({
      type: 'save_scene_dialog'
    });
  }

  public sendMenuState(open: boolean, rect?: MenuRect, ownerId: string = 'default'): void {
    if (open && rect && rect.width > 0 && rect.height > 0) {
      this.activeOverlays.set(ownerId, rect);
    } else {
      if (!this.activeOverlays.has(ownerId)) {
        return;
      }
      this.activeOverlays.delete(ownerId);
    }

    const rects: MenuRect[] = Array.from(this.activeOverlays.values());
    const isOpen = rects.length > 0;
    const primary = rects[rects.length - 1] ?? { x: 0, y: 0, width: 0, height: 0 };

    this.postJson({
      type: 'menu_state',
      open: isOpen,
      x: Math.max(0, Math.round(primary.x)),
      y: Math.max(0, Math.round(primary.y)),
      width: Math.max(0, Math.round(primary.width)),
      height: Math.max(0, Math.round(primary.height)),
      rects: rects.map((r) => ({
        x: Math.max(0, Math.round(r.x)),
        y: Math.max(0, Math.round(r.y)),
        width: Math.max(0, Math.round(r.width)),
        height: Math.max(0, Math.round(r.height))
      }))
    });
  }

  public sendGizmoTool(tool: GizmoTool): void {
    this.postJson({
      type: 'gizmo_tool',
      tool
    });
  }

  public sendCreateEntity(name: string, entityType: string = 'empty'): void {
    this.postJson({
      type: 'create_entity',
      name,
      entityType
    });
  }

  public sendDeleteEntity(entityId: number): void {
    this.postJson({
      type: 'delete_entity',
      entityId
    });
  }

  public sendDuplicateEntity(entityId: number): void {
    this.postJson({
      type: 'duplicate_entity',
      entityId
    });
  }

  public sendRenameEntity(entityId: number, name: string): void {
    this.postJson({
      type: 'rename_entity',
      entityId,
      name
    });
  }

  public sendAddComponent(entityId: number, componentType: string): void {
    this.postJson({
      type: 'add_component',
      entityId,
      componentType
    });
  }

  public sendImportModel(filePath: string): void {
    this.postJson({
      type: 'import_model',
      filePath
    });
  }

  public sendOpenImportDialog(): void {
    this.postJson({
      type: 'open_import_dialog'
    });
  }

  public requestSync(): void {
    this.postJson({
      type: 'request_sync'
    });
  }

  public sendCompileScripts(): void {
    this.postJson({
      type: 'compile_scripts'
    });
  }

  public sendBuildGame(): void {
    this.postJson({
      type: 'build_game'
    });
  }

  /**
   * Encodes a 16-byte header + 48-byte TransformComponent payload into a single
   * contiguous 64-byte ArrayBuffer (Float32Array + BigUint64Array) with zero JSON overhead.
   */
  public sendTransformMutation(state: EntityTransformState): ArrayBuffer {
    const buffer = new ArrayBuffer(PACKET_HEADER_BYTES + 48);
    const view = new DataView(buffer);

    // 16-byte BinaryPacketHeader
    view.setUint32(0, BINARY_IPC_MAGIC, true);
    view.setUint16(4, BinaryIPCOpcode.SyncEntityTransform, true);
    view.setUint16(6, BINARY_IPC_VERSION, true);
    view.setUint32(8, this.sequenceId++, true);
    view.setUint32(12, 48, true);

    // 48-byte BinaryTransformPayload
    view.setBigUint64(16, state.packedEntityId, true);
    const floats = new Float32Array(buffer, 24, 10);
    floats[0] = state.position[0];
    floats[1] = state.position[1];
    floats[2] = state.position[2];
    floats[3] = state.rotationQuat[0];
    floats[4] = state.rotationQuat[1];
    floats[5] = state.rotationQuat[2];
    floats[6] = state.rotationQuat[3];
    floats[7] = state.scale[0];
    floats[8] = state.scale[1];
    floats[9] = state.scale[2];

    this.postRawBuffer(buffer);
    return buffer;
  }

  public sendSelectEntity(packedEntityId: bigint): ArrayBuffer {
    const buffer = new ArrayBuffer(PACKET_HEADER_BYTES + 8);
    const view = new DataView(buffer);
    view.setUint32(0, BINARY_IPC_MAGIC, true);
    view.setUint16(4, BinaryIPCOpcode.SelectEntity, true);
    view.setUint16(6, BINARY_IPC_VERSION, true);
    view.setUint32(8, this.sequenceId++, true);
    view.setUint32(12, 8, true);
    view.setBigUint64(16, packedEntityId, true);

    this.postRawBuffer(buffer);
    return buffer;
  }

  public sendViewportPick(normX: number, normY: number): ArrayBuffer {
    const buffer = new ArrayBuffer(PACKET_HEADER_BYTES + 8);
    const view = new DataView(buffer);
    view.setUint32(0, BINARY_IPC_MAGIC, true);
    view.setUint16(4, BinaryIPCOpcode.ViewportPick, true);
    view.setUint16(6, BINARY_IPC_VERSION, true);
    view.setUint32(8, this.sequenceId++, true);
    view.setUint32(12, 8, true);
    view.setFloat32(16, normX, true);
    view.setFloat32(20, normY, true);

    this.postRawBuffer(buffer);
    return buffer;
  }

  public sendCommand(command: string): ArrayBuffer {
    this.postJson({
      type: 'console_command',
      command
    });

    const encoded = new TextEncoder().encode(command);
    const buffer = new ArrayBuffer(PACKET_HEADER_BYTES + encoded.byteLength);
    const view = new DataView(buffer);
    view.setUint32(0, BINARY_IPC_MAGIC, true);
    view.setUint16(4, BinaryIPCOpcode.ExecuteCommand, true);
    view.setUint16(6, BINARY_IPC_VERSION, true);
    view.setUint32(8, this.sequenceId++, true);
    view.setUint32(12, encoded.byteLength, true);
    new Uint8Array(buffer, PACKET_HEADER_BYTES).set(encoded);

    this.postRawBuffer(buffer);
    return buffer;
  }

  public decodeBinaryBatch(buffer: ArrayBuffer): number {
    const view = new DataView(buffer);
    let offset = 0;
    let decodedCount = 0;

    while (offset + PACKET_HEADER_BYTES <= buffer.byteLength) {
      const magic = view.getUint32(offset, true);
      if (magic !== BINARY_IPC_MAGIC) {
        break;
      }

      const opcode = view.getUint16(offset + 4, true) as BinaryIPCOpcode;
      const payloadBytes = view.getUint32(offset + 12, true);
      offset += PACKET_HEADER_BYTES;

      if (offset + payloadBytes > buffer.byteLength) {
        break;
      }

      if (opcode === BinaryIPCOpcode.SyncEntityTransform && payloadBytes === 48) {
        const packedEntityId = view.getBigUint64(offset, true);
        const floats = new Float32Array(buffer, offset + 8, 10);
        const transformState: EntityTransformState = {
          packedEntityId,
          position: [floats[0], floats[1], floats[2]],
          rotationQuat: [floats[3], floats[4], floats[5], floats[6]],
          scale: [floats[7], floats[8], floats[9]]
        };
        this.transformListeners.forEach((cb) => cb(transformState));
      } else if (opcode === BinaryIPCOpcode.EngineTelemetrySnapshot && payloadBytes === 32) {
        const telemetry: EngineTelemetryState = {
          frameTimeMs: view.getFloat32(offset, true),
          fps: view.getFloat32(offset + 4, true),
          aliveEntities: view.getUint32(offset + 8, true),
          physicsActiveBodies: view.getUint32(offset + 12, true),
          hotReloadGeneration: view.getUint32(offset + 16, true),
          renderGraphPasses: view.getUint32(offset + 20, true),
          aliasedTexturesReused: view.getUint32(offset + 24, true),
          audioVoicesPlayed: view.getUint32(offset + 28, true)
        };
        this.telemetryListeners.forEach((cb) => cb(telemetry));
      } else if (opcode === BinaryIPCOpcode.SelectEntity && payloadBytes === 8) {
        const packedEntityId = view.getBigUint64(offset, true);
        this.selectionListeners.forEach((cb) => cb(packedEntityId));
      } else if (opcode === BinaryIPCOpcode.ViewportResize && payloadBytes === 24) {
        const vp: SharedViewportState = {
          width: view.getUint32(offset, true),
          height: view.getUint32(offset + 4, true),
          dpiScale: view.getFloat32(offset + 8, true),
          sharedTextureHandle: view.getBigUint64(offset + 16, true)
        };
        this.viewportListeners.forEach((cb) => cb(vp));
      }

      offset += payloadBytes;
      decodedCount++;
    }

    return decodedCount;
  }

  private postRawBuffer(buffer: ArrayBuffer): void {
    if (typeof window !== 'undefined' && window.chrome?.webview) {
      window.chrome.webview.postMessage(buffer);
    }
  }
}

export const nativeBridge = new MorbiNativeBridge();
