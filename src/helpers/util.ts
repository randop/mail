import { AppError } from "@/lib/errors";
import {
  FailureResult,
  PartialSessionSchema,
  SuccessResult,
  type PartialSessionData,
  type SessionHostCheckResult,
} from "@/lib/types";
import isValidDomain from "is-valid-domain";

export function verifyEmailDomain(value: string): boolean {
  return isValidDomain(value, {
    subdomain: true,
    wildcard: false,
    allowUnicode: true,
    topLevel: false,
  });
}

/**
 * Checks hostname domain from EHLO command of session
 * @param session Partial session data
 * @returns SuccessResult if valid, FailureResult otherwise
 */
export function checkSessionHostname(
  session: PartialSessionData,
): SessionHostCheckResult {
  const parseResult = PartialSessionSchema.safeParse(session);
  const ip: string = session?.remoteAddress ?? "?0.0.0.0";
  if (parseResult.success && verifyEmailDomain(session.hostNameAppearAs)) {
    return SuccessResult(session.hostNameAppearAs);
  }

  return FailureResult(
    new AppError(
      `Check session hostname error from ip: ${ip}`,
      "SMTP_HOSTNAME_ERROR",
    ),
  );
}
