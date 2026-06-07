import { readdirSync, readFileSync, statSync } from "node:fs";
import { homedir, networkInterfaces } from "node:os";
import { join } from "node:path";

type JsonRpcMessage = {
  id?: number | string;
  result?: unknown;
  error?: { code?: number; message?: string };
  method?: string;
  params?: unknown;
};

type PendingRequest = {
  resolve: (message: JsonRpcMessage) => void;
  reject: (error: Error) => void;
  timer: ReturnType<typeof setTimeout>;
};

type QuotaWindow = {
  label: string;
  usedPercent: number;
  remainingPercent: number;
  resetsAt: number | null;
  windowDurationMins: number | null;
};

const host = Bun.env.ESPAI_HOST || "0.0.0.0";
const port = Number(Bun.env.ESPAI_PORT || "8787");
const codexBin = Bun.env.CODEX_BIN || "/Applications/Codex.app/Contents/Resources/codex";

class CodexQuotaClient {
  private proc: ReturnType<typeof Bun.spawn> | null = null;
  private writer: {
    write(data: string | Uint8Array): number | Promise<number>;
    flush?: () => void | Promise<void>;
    end?: () => void;
  } | null = null;
  private pending = new Map<number, PendingRequest>();
  private nextId = 1;
  private initialized = false;
  private starting: Promise<void> | null = null;

  async readQuota() {
    await this.ensureStarted();
    await this.initialize();
    const message = await this.request("account/rateLimits/read", null);
    if (message.error) {
      throw new Error(message.error.message || `Codex RPC error ${message.error.code ?? ""}`);
    }
    return normalizeQuota(message.result);
  }

  private async ensureStarted() {
    if (this.proc && this.writer) {
      return;
    }
    if (this.starting) {
      return this.starting;
    }

    this.starting = this.start();
    try {
      await this.starting;
    } finally {
      this.starting = null;
    }
  }

  private async start() {
    this.stop();

    const proc = Bun.spawn([codexBin, "app-server", "--listen", "stdio://"], {
      stdin: "pipe",
      stdout: "pipe",
      stderr: "pipe",
    });

    this.proc = proc;
    this.writer = proc.stdin;
    this.initialized = false;
    this.readStdout(proc.stdout);
    this.drainStderr(proc.stderr);

    proc.exited.then(() => {
      this.rejectAll(new Error("codex app-server exited"));
      this.proc = null;
      this.writer = null;
      this.initialized = false;
    });
  }

  private async initialize() {
    if (this.initialized) {
      return;
    }
    const message = await this.request("initialize", {
      clientInfo: {
        name: "espai",
        title: "espai",
        version: "0.1.0",
      },
      capabilities: {
        experimentalApi: true,
        optOutNotificationMethods: [],
      },
    });
    if (message.error) {
      throw new Error(message.error.message || "Codex initialize failed");
    }
    await this.notify("initialized", {});
    this.initialized = true;
  }

  shutdown() {
    this.stop();
  }

  private async request(method: string, params: unknown, timeoutMs = 3000): Promise<JsonRpcMessage> {
    const writer = this.writer;
    if (!writer) {
      throw new Error("codex app-server is not running");
    }

    const id = this.nextId++;
    const payload = JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n";
    const promise = new Promise<JsonRpcMessage>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`Codex RPC timeout: ${method}`));
      }, timeoutMs);
      this.pending.set(id, { resolve, reject, timer });
    });

    await writer.write(payload);
    await writer.flush?.();
    return promise;
  }

  private async notify(method: string, params: unknown) {
    const writer = this.writer;
    if (!writer) {
      return;
    }
    const payload = JSON.stringify({ jsonrpc: "2.0", method, params }) + "\n";
    await writer.write(payload);
    await writer.flush?.();
  }

  private async readStdout(stream: ReadableStream<Uint8Array>) {
    const reader = stream.getReader();
    const decoder = new TextDecoder();
    let buffer = "";

    try {
      while (true) {
        const { value, done } = await reader.read();
        if (done) {
          break;
        }
        buffer += decoder.decode(value, { stream: true });
        let newlineIndex = buffer.indexOf("\n");
        while (newlineIndex >= 0) {
          const line = buffer.slice(0, newlineIndex).trim();
          buffer = buffer.slice(newlineIndex + 1);
          this.handleLine(line);
          newlineIndex = buffer.indexOf("\n");
        }
      }
    } catch (error) {
      this.rejectAll(error instanceof Error ? error : new Error(String(error)));
    }
  }

  private async drainStderr(stream: ReadableStream<Uint8Array>) {
    const reader = stream.getReader();
    while (!(await reader.read()).done) {
      // Keep draining stderr so app-server warnings cannot block the process.
    }
  }

  private handleLine(line: string) {
    if (!line) {
      return;
    }
    let message: JsonRpcMessage;
    try {
      message = JSON.parse(line);
    } catch {
      return;
    }

    const id = typeof message.id === "string" ? Number(message.id) : message.id;
    if (typeof id !== "number") {
      return;
    }

    const pending = this.pending.get(id);
    if (!pending) {
      return;
    }
    clearTimeout(pending.timer);
    this.pending.delete(id);
    pending.resolve(message);
  }

  private rejectAll(error: Error) {
    for (const [id, pending] of this.pending.entries()) {
      clearTimeout(pending.timer);
      pending.reject(error);
      this.pending.delete(id);
    }
  }

  private stop() {
    this.rejectAll(new Error("codex app-server restarting"));
    this.writer?.end?.();
    this.proc?.kill();
    this.proc = null;
    this.writer = null;
    this.initialized = false;
  }
}

function normalizeQuota(result: unknown) {
  if (!isRecord(result)) {
    throw new Error("Invalid Codex quota response");
  }

  const byLimitId = isRecord(result.rateLimitsByLimitId) ? result.rateLimitsByLimitId : null;
  const codexBucket = byLimitId && isRecord(byLimitId.codex) ? byLimitId.codex : null;
  const fallbackBucket = isRecord(result.rateLimits) ? result.rateLimits : null;
  const bucket = codexBucket || fallbackBucket;

  if (!bucket) {
    throw new Error("No Codex rate limit bucket found");
  }

  const primary = parseWindow(bucket.primary);
  if (!primary) {
    throw new Error("Codex quota primary window is missing");
  }

  const secondary = parseWindow(bucket.secondary);
  const updatedAt = Math.floor(Date.now() / 1000);

  return {
    ok: true,
    source: codexBucket ? "codex-app-server" : "codex-app-server-fallback",
    limitId: typeof bucket.limitId === "string" ? bucket.limitId : null,
    limitName: typeof bucket.limitName === "string" ? bucket.limitName : null,
    planType: typeof bucket.planType === "string" ? bucket.planType : null,
    primary,
    secondary,
    updatedAt,
  };
}

function readQuotaFromSessionLogs() {
  const sessionDir = join(homedir(), ".codex", "sessions");
  const files = newestJsonlFiles(sessionDir, 120);

  for (const file of files) {
    const snapshot = parseSessionFile(file.path);
    if (snapshot) {
      return snapshot;
    }
  }

  throw new Error("No Codex quota snapshot found in ~/.codex/sessions");
}

function newestJsonlFiles(root: string, limit: number) {
  const files: { path: string; mtimeMs: number }[] = [];

  function visit(dir: string) {
    let entries: string[];
    try {
      entries = readdirSync(dir);
    } catch {
      return;
    }

    for (const entry of entries) {
      const path = join(dir, entry);
      let stat;
      try {
        stat = statSync(path);
      } catch {
        continue;
      }
      if (stat.isDirectory()) {
        visit(path);
      } else if (stat.isFile() && path.endsWith(".jsonl")) {
        files.push({ path, mtimeMs: stat.mtimeMs });
      }
    }
  }

  visit(root);
  return files.sort((a, b) => b.mtimeMs - a.mtimeMs).slice(0, limit);
}

function parseSessionFile(filePath: string) {
  let text: string;
  try {
    const raw = readFileSync(filePath);
    const tail = raw.length > 4 * 1024 * 1024 ? raw.subarray(raw.length - 4 * 1024 * 1024) : raw;
    text = tail.toString("utf8");
  } catch {
    return null;
  }

  const lines = text.split("\n");
  for (let i = lines.length - 1; i >= 0; i--) {
    const line = lines[i].trim();
    if (!line) {
      continue;
    }

    let payload: unknown;
    try {
      payload = JSON.parse(line);
    } catch {
      continue;
    }
    if (!isRecord(payload) || payload.type !== "event_msg" || !isRecord(payload.payload)) {
      continue;
    }

    const inner = payload.payload;
    if (inner.type !== "token_count" || !isRecord(inner.rate_limits)) {
      continue;
    }

    const rateLimits = inner.rate_limits;
    const limitId = typeof rateLimits.limit_id === "string" ? rateLimits.limit_id.toLowerCase() : "";
    if (limitId !== "codex") {
      continue;
    }

    const primary = parseSessionWindow(rateLimits.primary);
    if (!primary) {
      continue;
    }

    return {
      ok: true,
      source: "codex-session-log",
      sourceFileName: filePath.split("/").pop() || filePath,
      limitId: "codex",
      limitName: typeof rateLimits.limit_name === "string" ? rateLimits.limit_name : null,
      planType: typeof rateLimits.plan_type === "string" ? rateLimits.plan_type : null,
      primary,
      secondary: parseSessionWindow(rateLimits.secondary),
      updatedAt: timestampToEpoch(payload.timestamp) || Math.floor(Date.now() / 1000),
    };
  }

  return null;
}

function parseSessionWindow(value: unknown): QuotaWindow | null {
  if (!isRecord(value)) {
    return null;
  }
  const usedPercent = clampPercent(toNumber(value.used_percent));
  const windowDurationMins = toNumber(value.window_minutes);
  return {
    label: windowLabel(windowDurationMins),
    usedPercent,
    remainingPercent: Math.max(0, 100 - usedPercent),
    resetsAt: toNumber(value.resets_at),
    windowDurationMins,
  };
}

function timestampToEpoch(value: unknown) {
  if (typeof value !== "string") {
    return null;
  }
  const ms = Date.parse(value);
  return Number.isFinite(ms) ? Math.floor(ms / 1000) : null;
}

function parseWindow(value: unknown): QuotaWindow | null {
  if (!isRecord(value)) {
    return null;
  }
  const usedPercent = clampPercent(toNumber(value.usedPercent));
  const windowDurationMins = toNumber(value.windowDurationMins);
  return {
    label: windowLabel(windowDurationMins),
    usedPercent,
    remainingPercent: Math.max(0, 100 - usedPercent),
    resetsAt: toNumber(value.resetsAt),
    windowDurationMins,
  };
}

function windowLabel(minutes: number | null) {
  if (minutes === 300) return "5h";
  if (minutes === 10080) return "7d";
  if (minutes && minutes % 1440 === 0) return `${minutes / 1440}d`;
  if (minutes && minutes % 60 === 0) return `${minutes / 60}h`;
  return minutes ? `${minutes}m` : "?";
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null;
}

function toNumber(value: unknown): number | null {
  if (typeof value === "number" && Number.isFinite(value)) {
    return value;
  }
  if (typeof value === "string" && value.trim() !== "") {
    const parsed = Number(value);
    return Number.isFinite(parsed) ? parsed : null;
  }
  return null;
}

function clampPercent(value: number | null) {
  if (value === null) {
    return 0;
  }
  return Math.min(100, Math.max(0, Math.round(value)));
}

function json(data: unknown, init: ResponseInit = {}) {
  return new Response(JSON.stringify(data, null, 2), {
    ...init,
    headers: {
      "content-type": "application/json; charset=utf-8",
      "cache-control": "no-store",
      ...(init.headers || {}),
    },
  });
}

function localIPv4Addresses() {
  const addresses: string[] = [];
  for (const interfaces of Object.values(networkInterfaces())) {
    for (const item of interfaces || []) {
      if (item.family === "IPv4" && !item.internal) {
        addresses.push(item.address);
      }
    }
  }
  return addresses;
}

function logQuota(source: string, quota: ReturnType<typeof normalizeQuota> | ReturnType<typeof readQuotaFromSessionLogs>) {
  const primary = quota.primary;
  const secondary = quota.secondary;
  const secondaryText = secondary ? `, ${secondary.label} ${secondary.remainingPercent}%` : "";
  console.log(`[${new Date().toLocaleString()}] ${source}: ${primary.label} ${primary.remainingPercent}%${secondaryText}`);
}

const quotaClient = new CodexQuotaClient();

Bun.serve({
  hostname: host,
  port,
  idleTimeout: 30,
  async fetch(request) {
    const url = new URL(request.url);

    if (url.pathname === "/health") {
      return json({ ok: true, service: "espai-mac-service" });
    }

    if (url.pathname !== "/api/status") {
      return json({ ok: false, error: "not_found" }, { status: 404 });
    }

    try {
      const quota = await quotaClient.readQuota();
      logQuota("app-server", quota);
      return json(quota);
    } catch (error) {
      quotaClient.shutdown();
      try {
        const quota = readQuotaFromSessionLogs();
        console.log(`[${new Date().toLocaleString()}] app-server failed: ${error instanceof Error ? error.message : String(error)}`);
        logQuota("session-log", quota);
        return json(quota);
      } catch (fallbackError) {
        console.log(`[${new Date().toLocaleString()}] quota failed: ${fallbackError instanceof Error ? fallbackError.message : String(fallbackError)}`);
        return json({
          ok: false,
          error: fallbackError instanceof Error ? fallbackError.message : String(fallbackError),
          appServerError: error instanceof Error ? error.message : String(error),
          updatedAt: Math.floor(Date.now() / 1000),
        }, { status: 503 });
      }
    }
  },
});

console.log(`espai Mac service listening on http://${host}:${port}`);
console.log("ESP32 setup can use one of these Mac hosts:");
for (const address of localIPv4Addresses()) {
  console.log(`  ${address}`);
  console.log(`  http://${address}:${port}/api/status`);
}
