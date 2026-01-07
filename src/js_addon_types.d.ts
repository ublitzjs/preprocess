export enum Status { // < 0 - error, > 0 - good
  Ready = 1,
  PendingDiskRead = 0,
  CantUseFile = -1,
  CantAllocate = -2,
  AST_Failed = -3
}
export enum Purpose {
  JustRead = 0,
  Insert = 1,
  Remove = 2,
}
export enum SyntaxPositions {
  prefix = 0,
  insertOn = 1,
  removeOn = 2,
  end = 3
}

export type SyntaxBaseType = {
    prefix: string;
    insertOn: string;
    removeOn: string;
    end: string;
}

export type SyntaxPairType = {
  pattern: RegExp;
  data: Uint8Array;
}

export type Cache = { container: ArrayBuffer; status: Status; busy: number; ast: Uint32Array; data: ArrayBuffer }

export type Instructions<T> = {
  t_id: number;
  slots?: Record<string, Instructions<T> | Instructions<T>[]>;
  data?: Record<string, T> | Record<string, T>[]
}
