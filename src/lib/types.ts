import { AppError } from "@/lib/errors";
import * as z from "zod";

export type NullableString = string | null;

export type Result<T, E = AppError> =
  | { readonly ok: true; readonly value: T }
  | { readonly ok: false; readonly error: E };

export const SuccessResult = <T>(value: T) => ({ ok: true, value }) as const;
export const FailureResult = <E>(error: E) => ({ ok: false, error }) as const;

export type SessionHostCheckResult = Result<string, AppError>;

export const PartialSessionSchema = z.object({
  hostNameAppearAs: z.string(),
  remoteAddress: z.string(),
});

export type PartialSessionData = z.infer<typeof PartialSessionSchema>;
