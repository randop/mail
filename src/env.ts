import { z } from "zod";
const log = console;

const envSchema = z.object({
  NODE_ENV: z
    .enum(["development", "test", "production"])
    .default("development"),
  REDIS_URL: z.string().url(),
  LOG_LEVEL: z
    .enum(["debug", "info", "warn", "error"])
    .optional()
    .default("info"),
});

const parsed = envSchema.safeParse(process.env);

if (!parsed.success) {
  log.error("Invalid or missing environment variables:");
  log.error(parsed.error.format());
  process.exit(1);
}

export const env = parsed.data;

// Optional: Infer type for use elsewhere
export type Env = z.infer<typeof envSchema>;
