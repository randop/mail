import { env } from "@/env";
import { createClient } from "redis";
type RedisClient = ReturnType<typeof createClient>;

import log from "@/helpers/log";

interface CacheClient {
  /**
   * Retrieve a value from the cache by key
   * @param key The key to retrieve
   * @returns The value (string | null)
   */
  get(key: string): Promise<string | null>;

  /**
   * Check if item exist on the cache by key
   * @param key The key to check
   * @returns true if the key exists, false otherwise
   */
  has(key: string): Promise<boolean>;

  /**
   * Sets a value in the cache
   *
   * @param key The key to set
   * @param value The value to store
   * @param ttl Optional TTL in seconds
   * @returns true if the key was set, false otherwise
   */
  set(key: string, value: string, ttl?: number): Promise<boolean>;

  /**
   * Removes one or more keys from the cache
   * @param key Single key or array of keys
   * @returns Number of keys actually removed
   */
  remove(key: string | string[]): Promise<number>;
}

export class CacheService implements CacheClient {
  private client: RedisClient;

  constructor(connectionUrl?: string | null) {
    if (connectionUrl && connectionUrl.trim() !== "") {
      this.client = createClient({ url: connectionUrl.trim() });
    } else {
      this.client = createClient({ url: env.REDIS_URL });
    }
    this.client.on("error", (err) =>
      log.error("Cache service redis client error: ", err),
    );
  }

  async connect(): Promise<void> {
    await this.client.connect();
  }

  async get(key: string): Promise<string | null> {
    try {
      return await this.client.get(key);
    } catch {
      return null;
    }
  }

  async has(key: string): Promise<boolean> {
    const result = await this.client.exists(key);
    return result === 1;
  }

  async set(key: string, value: string, ttl?: number): Promise<boolean> {
    try {
      if (typeof ttl === "number" && ttl > 0) {
        await this.client.set(key, value, {
          EX: ttl,
        });
      } else {
        await this.client.set(key, value);
      }
    } catch {
      return false;
    }
    return true;
  }

  async remove(key: string | string[]): Promise<number> {
    try {
      return await this.client.del(key);
    } catch {
      return 0;
    }
  }

  close() {
    this.client.destroy();
  }
}

export default CacheService;
