import "zod";

declare global {
  namespace NodeJS {
    interface ProcessEnv {
      NODE_ENV: "development" | "test" | "production";

      REDIS_URL: string;

      LOG_LEVEL?: "debug" | "info" | "warn" | "error";
    }
  }
}

// Important: This line ensures the file is treated as a module-augmenting file
// Without it, TypeScript might ignore the declaration in some configurations
export {};
