import type { ReadStream } from "node:fs";

export enum PurposeEnum {
  JustRead = 0,
  Insert = 1,
  Remove = 2,
}

export type UserParams = {
  templates: string[];
  progressCB(data: string): void | Promise<void>
}

export type SyntaxType = {
  pattern: RegExp;
  prefix: string;
  insertOn: string;
  removeOn: string;
  end: string;
  maxActionL: number;
}

export type Instructions = {
  t_id: number;
  slots?: Record<string, (Instructions | string)[]>;
  data?: Record<string, any> | Record<string, any>[]
}

export enum StatusEnum {
  pending = 0,
  not_processed = 1,
  processed = 2,
  streamed = 3,
  failed = 4
}

export type CacheT<T extends StatusEnum = StatusEnum> = {
  ast: number[] | Uint32Array
  busy: number;
  status: T;
  content: T extends StatusEnum.processed
  ? string 
  : T extends StatusEnum.not_processed
  ? string
  : T extends StatusEnum.pending 
  ? Promise<string>
  : ReadStream
}

export type SharedT = {
  result: string;
  aborted: boolean;
  caches: CacheT[]
  stack: (Record<string, any> | undefined)[]
}
