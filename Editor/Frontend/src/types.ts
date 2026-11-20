// ============================================================================
// MorbiEngine - Editor UI Types & Data Models
// ============================================================================

export interface MeshRendererData {
  meshAssetId: number;
  submeshIndex: number;
  flags: number;
  name: string;
}

export type MotionTypeKind = 'Static' | 'Kinematic' | 'Dynamic';
export type ColliderShapeKind = 'Box' | 'Sphere' | 'Capsule';

export interface RigidbodyData {
  motionType: MotionTypeKind;
  shape: ColliderShapeKind;
  mass: number;
  density: number;
  useDensity: boolean;
  friction: number;
  restitution: number;
  linearDamping: number;
  angularDamping: number;
  useGravity: boolean;
  gravityFactor: number;
  useCCD: boolean;
  isTrigger: boolean;
  isAwake?: boolean;
  collisionLayer: number;
  collisionMask: number;
  lockedAxes?: number;
  halfExtents: [number, number, number];
  colliderOffset: [number, number, number];
  colliderRotationEuler?: [number, number, number];
}

export interface CompoundColliderSubShapeData {
  shape: ColliderShapeKind;
  halfExtents: [number, number, number];
  localOffset: [number, number, number];
  localRotationEuler: [number, number, number];
  collisionLayer: number;
  collisionMask: number;
  isTrigger: boolean;
}

export type PhysicsJointKind = 'Fixed' | 'Distance' | 'Hinge' | 'Slider';

export interface PhysicsJointData {
  type: PhysicsJointKind;
  connectedEntityId: number;
  connectedEntityName: string;
  anchorA: [number, number, number];
  anchorB: [number, number, number];
  axis: [number, number, number];
  autoDetectAnchors: boolean;
  minDistance: number;
  maxDistance: number;
  springFrequencyHz: number;
  springDampingRatio: number;
  minLimit: number;
  maxLimit: number;
  maxFriction: number;
  breakForce: number;
  breakTorque: number;
  enabled: boolean;
  broken?: boolean;
}

export interface CharacterControllerData {
  radius: number;
  height: number;
  mass: number;
  maxPushStrength: number;
  stepHeight: number;
  maxSlopeAngleDegrees: number;
  gravityFactor: number;
  airControl: number;
  isGrounded?: boolean;
  isSliding?: boolean;
}

export interface ScriptData {
  className: string;
  namespace?: string;
}

export interface LightData {
  type: string;
  intensity: number;
  color: [number, number, number];
}

export interface CameraData {
  fov: number;
  near: number;
  far: number;
  aspect?: number;
  isActive: boolean;
}

export interface ComponentData {
  transform: {
    position: [number, number, number];
    rotation: [number, number, number];
    scale: [number, number, number];
  };
  tag: string;
  layer: string;
  meshRenderer?: MeshRendererData;
  rigidbody?: RigidbodyData;
  compoundColliders?: CompoundColliderSubShapeData[];
  physicsJoints?: PhysicsJointData[];
  script?: ScriptData;
  light?: LightData;
  camera?: CameraData;
  characterController?: CharacterControllerData;
  animator?: {
    controller?: string;
    currentState?: string;
    clipName?: string;
    isPlaying?: boolean;
    playbackSpeed?: number;
  };
  playerInput?: {
    map: string;
    scheme: string;
  };
  health?: {
    maxHp: number;
    currentHp: number;
    shield: number;
  };
  weapons?: {
    primary: string;
    secondary: string;
    ammo: number;
  };
}

export interface HierarchyItem {
  id: string;
  entityId?: number;
  name: string;
  type: 'folder' | 'entity';
  iconType: 'camera' | 'light' | 'cloud' | 'mountain' | 'avatar' | 'pivot' | 'folder' | 'cube';
  children?: HierarchyItem[];
  componentData?: ComponentData;
}

export type GizmoTool = 'select' | 'translate' | 'rotate' | 'scale' | 'sculpt' | 'foliage' | 'magnet' | 'settings';

export interface AssetItem {
  id: string;
  name: string;
  type: 'folder' | 'prefab' | 'model' | 'texture' | 'material' | 'scene' | 'script' | 'file';
  path: string;
  sizeBytes?: number;
  thumbnailColor?: string;
  iconType?: string;
}

export interface ConsoleLogMessage {
  id: string;
  level: 'info' | 'warning' | 'error' | 'debug';
  category?: string;
  message: string;
  timestamp: string;
}
