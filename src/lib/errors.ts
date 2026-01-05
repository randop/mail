/**
 * Centralized, strongly-typed error codes used across the application
 */
export const AppErrorCode = {
	// *** 4xx Client Errors ***
	BAD_REQUEST: "BAD_REQUEST",
	VALIDATION_ERROR: "VALIDATION_ERROR",
	UNAUTHORIZED: "UNAUTHORIZED",
	FORBIDDEN: "FORBIDDEN",
	NOT_FOUND: "NOT_FOUND",
	CONFLICT: "CONFLICT",
	TOO_MANY_REQUESTS: "TOO_MANY_REQUESTS",
	PAYMENT_REQUIRED: "PAYMENT_REQUIRED",

	// *** 5xx Server Errors ***
	INTERNAL_ERROR: "INTERNAL_ERROR",
	SERVICE_UNAVAILABLE: "SERVICE_UNAVAILABLE",
	EXTERNAL_SERVICE_FAILURE: "EXTERNAL_SERVICE_FAILURE",
	DATABASE_ERROR: "DATABASE_ERROR",
	TIMEOUT: "TIMEOUT",

	// *** Custom Errors ***
	SMTP_HOSTNAME_ERROR: "SMTP_HOSTNAME_ERROR",
} as const;

export type AppErrorCodeType = keyof typeof AppErrorCode;

/**
 * Maps error codes to their status codes
 */
export const ErrorCodeToStatus: Record<AppErrorCodeType, number> = {
	BAD_REQUEST: 400,
	VALIDATION_ERROR: 400,
	UNAUTHORIZED: 401,
	FORBIDDEN: 403,
	NOT_FOUND: 404,
	CONFLICT: 409,
	TOO_MANY_REQUESTS: 429,
	PAYMENT_REQUIRED: 402,
	INTERNAL_ERROR: 500,
	SERVICE_UNAVAILABLE: 503,
	EXTERNAL_SERVICE_FAILURE: 502,
	DATABASE_ERROR: 503,
	TIMEOUT: 504,
	SMTP_HOSTNAME_ERROR: 503,
} as const;

/**
 * Base class for all known/handled application errors.
 * Extend this for domain-specific errors (ValidationError, NotFoundError, etc.)
 */
export class AppError extends Error {
	public readonly code: AppErrorCodeType; // short machine-readable code
	public readonly statusCode: number; // HTTP status if applicable
	public readonly details?: unknown; // extra context (object, array, etc.)
	public readonly isOperational: boolean; // true = safe to expose to client, false = internal
	public readonly correlationId?: string; // for tracing across services

	constructor(
		message: string,
		code: AppErrorCodeType,
		statusCode?: number,
		details?: unknown,
		correlationId?: string,
	) {
		super(message);

		this.name = "AppError";
		this.code = code;
		this.statusCode =
			statusCode ?? ErrorCodeToStatus[code] ?? ErrorCodeToStatus.INTERNAL_ERROR;
		this.details = details;
		this.correlationId = correlationId;

		// Proper prototype chain for instanceof in extended classes
		Object.setPrototypeOf(this, new.target.prototype);
	}

	static badRequest(message: string, details?: unknown): AppError {
		return new AppError(message, AppErrorCode.BAD_REQUEST, undefined, details);
	}

	static unauthorized(message = "Unauthorized"): AppError {
		return new AppError(message, AppErrorCode.UNAUTHORIZED);
	}

	static forbidden(message = "Forbidden"): AppError {
		return new AppError(message, AppErrorCode.FORBIDDEN);
	}

	static notFound(resource: string): AppError {
		return new AppError(`${resource} not found`, AppErrorCode.NOT_FOUND);
	}

	static conflict(message: string): AppError {
		return new AppError(message, AppErrorCode.CONFLICT);
	}

	static tooManyRequests(message = "Rate limit exceeded"): AppError {
		return new AppError(message, AppErrorCode.TOO_MANY_REQUESTS);
	}

	static internal(
		message = "An unexpected error occurred",
		details?: unknown,
	): AppError {
		return new AppError(
			message,
			AppErrorCode.INTERNAL_ERROR,
			undefined,
			details,
		);
	}
}
