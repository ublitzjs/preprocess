type Flatten<T, Prev extends string = ''> = {
  [K in keyof T]: T[K] extends object
    ? T[K] extends infer O extends object
      ? Flatten<O, `${Prev}${Prev extends '' ? '' : '.'}${Extract<K, string>}`>
      : never
    : {
        [P in `${Prev}${Prev extends '' ? '' : '.'}${Extract<K, string>}`]: T[K]
      }
}[keyof T];
type UnionToIntersection<U> =
  (U extends any ? (k: U) => void : never) extends
  (k: infer I) => void ? I : never;
type FlattenToObject<T> = UnionToIntersection<Flatten<T>>;

