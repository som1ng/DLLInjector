export class AdapterError extends Error {
  constructor(public readonly code: string, message: string) {
    super(message);
  }
}

export function publicError(error: unknown): AdapterError {
  return error instanceof AdapterError ? error :
    new AdapterError('INTERNAL', 'Unexpected adapter failure; inspect the local installation.');
}
