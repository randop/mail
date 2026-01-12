export interface QueryOptions {
  /** Custom base URL (default: official Spamhaus API) */
  baseUrl?: string;
  /** Preferred response format */
  accept?: "application/json";
}

export interface SpamhausResponse {
  resp: number[];
  status: number;
  [key: string]: any;
}

/**
 * Reputation status based on Spamhaus query result
 */
export enum ReputationStatus {
  /** Not listed in the queried blocklist – good/clean reputation */
  GOOD = "GOOD",
  /** Listed in the queried blocklist – bad reputation */
  BAD = "BAD",
  /** Query failed or unexpected response (e.g., auth error, rate limit) */
  UNKNOWN = "UNKNOWN",
  /** Reject incoming requests from this IP **/
  BANNED = "BANNED",
}

export interface ReputationResult<T = SpamhausResponse | string> {
  /** The reputation status */
  status: ReputationStatus;
  /** Raw response data (null if status is UNKNOWN or GOOD/not listed) */
  data: T | null;
  /** Human-readable message */
  message: string;
}

export enum ReputationDecision {
  UNKNOWN = "UNKNOWN",
  OK = "OK",
  THREAT = "THREAT",
  FRAUD = "FRAUD",
  MALICIOUS = "MALICIOUS",
}

export interface SessionReputation {
  status: ReputationStatus;
  decision: ReputationDecision;
}
