import { AppError } from "@/lib/errors";

/**
 * Converts any caught value (usually `unknown`) into a proper Error instance.
 *
 * - If it's already an Error → returns it as-is
 * - If it's a string → wraps it in new Error()
 * - If it's an object with .message → uses that message
 * - Otherwise → creates a generic Error with String() representation
 *
 * The original value is always preserved as the `cause`.
 */
export function toError(maybeError: unknown): Error {
  // Already an Error → pass through
  if (maybeError instanceof Error) {
    return maybeError;
  }

  // Most common manual throw: throw "something went wrong"
  if (typeof maybeError === "string") {
    return new Error(maybeError);
  }

  // Some libraries throw plain objects with message property
  if (
    maybeError != null &&
    typeof maybeError === "object" &&
    "message" in maybeError &&
    typeof (maybeError as any).message === "string"
  ) {
    const err = new Error((maybeError as any).message);
    err.cause = maybeError;
    return err;
  }

  // Fallback: everything else
  const fallbackMessage = String(maybeError ?? "Unknown error");
  const error = new Error(fallbackMessage);
  error.cause = maybeError;

  return error;
}

export function toAppError(maybeError: unknown): AppError {
  const error = toError(maybeError);
  return AppError.internal(error.message, error.cause);
}
