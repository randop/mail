import { request } from "undici";
import {
  QueryOptions,
  SpamhausResponse,
  ReputationStatus,
} from "@/lib/reputation";

const API_BASE = "https://apibl.spamhaus.net/lookup/v1/";

/**
 * Known HTTP error messages for Spamhaus API responses
 */
const ERROR_MESSAGES: Record<number, string> = {
  401: "Unauthorized – invalid DQS key",
  403: "Forbidden – check DQS key or account status",
  429: "Rate limited – too many requests",
};

export interface ReputationResult<T = SpamhausResponse | string> {
  /** The reputation status */
  status: ReputationStatus;
  /** Raw response data (null if status is UNKNOWN or GOOD/not listed) */
  data: T | null;
  /** Human-readable message */
  message: string;
}

export class ReputationQueryService {
  private readonly dqsKey: string;
  private readonly baseUrl: string;
  private readonly accept: string;

  constructor(dqsKey: string, options: QueryOptions = {}) {
    if (!dqsKey || dqsKey.length !== 26) {
      throw new Error(
        "API initialization error: Missing required Spamhaus DQS key",
      );
    }

    this.dqsKey = dqsKey;
    this.baseUrl = options.baseUrl || API_BASE;
    this.accept = options.accept || "application/json";
  }

  private buildResult<T>(
    status: ReputationStatus,
    data: T | null,
    message: string,
  ): ReputationResult<T> {
    return { status, data, message };
  }

  /**
   * Internal method that processes the HTTP request and maps the response to a ReputationResult
   */
  private async processQuery(resource: string): Promise<ReputationResult> {
    if (!resource || typeof resource !== "string") {
      return this.buildResult(
        ReputationStatus.UNKNOWN,
        null,
        "Resource must be a non-empty string",
      );
    }

    const url = `${this.baseUrl}${encodeURIComponent(resource)}`;

    try {
      const { statusCode, body } = await request(url, {
        method: "GET",
        headers: {
          Authorization: `Bearer ${this.dqsKey}`,
          Accept: this.accept,
        },
      });

      if (statusCode === 200) {
        const data =
          this.accept === "application/json"
            ? ((await body.json()) as SpamhausResponse)
            : await body.text();

        return this.buildResult(
          ReputationStatus.BAD,
          data,
          "Listed – bad reputation",
        );
      }

      if (statusCode === 404) {
        return this.buildResult(
          ReputationStatus.GOOD,
          null,
          "Not listed – good reputation",
        );
      }

      const text = await body.text();
      const message =
        ERROR_MESSAGES[statusCode] ||
        `HTTP ${statusCode}: ${text.trim() || "No details"}`;

      return this.buildResult(ReputationStatus.UNKNOWN, null, message);
    } catch (err) {
      const message = `API request error: ${(err as Error).message}`;
      return this.buildResult(ReputationStatus.UNKNOWN, null, message);
    }
  }

  async query(resource: string): Promise<ReputationResult> {
    return this.processZEN(resource);
  }

  private async queryList(
    list: string,
    resource: string,
  ): Promise<ReputationResult> {
    return this.processQuery(`${list}/${resource}`);
  }

  private async queryZEN(ip: string): Promise<ReputationResult> {
    return this.queryList("ZEN", ip);
  }

  private async queryDBL(domain: string): Promise<ReputationResult> {
    return this.queryList("DBL", domain);
  }

  private async queryAUTHBL(ip: string): Promise<ReputationResult> {
    return this.queryList("AUTHBL", ip);
  }
}

export default ReputationQueryService;
