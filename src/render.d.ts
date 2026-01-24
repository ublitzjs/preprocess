export function compile(
  template: string,
  regAbort: (cb:()=>void)=>void,
  shouldSave: boolean,
): Promise<void>;

export function render(
  params: UserParams,
  instructions: Instructions,
  regAbort: (cb:()=>void)=>void,
): Promise<void | Error>

export function manualCreateCache(name: string, content: string): void
export function tryToClearCache(name: string): void
export var allSyntax: SyntaxType[]

type UserParams = {
  templates: string[];
  progressCB(data: string): void | Promise<void>,
}

type SyntaxType = {
  pattern: RegExp;
  insertOn: string;
  end: string;
}

type Instructions = {
  t_id: number;
  slots?: Record<string, (Instructions | string)[]>;
  data?: Record<string, any> | Record<string, any>[]
}
