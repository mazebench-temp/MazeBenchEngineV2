import { mkdir, readFile, rename, writeFile } from "node:fs/promises";
import { dirname } from "node:path";
import { fileURLToPath } from "node:url";
import type { Plugin } from "vite";

const ENDPOINT = "/api/local-project";
const MAX_PROJECT_BYTES = 25 * 1024 * 1024;
const PROJECT_FILE = fileURLToPath(
  new URL("../../../project-data/voxelbench-project.json", import.meta.url),
);

function sendJson(response: import("node:http").ServerResponse, status: number, body: unknown) {
  response.statusCode = status;
  response.setHeader("Cache-Control", "no-store");
  response.setHeader("Content-Type", "application/json; charset=utf-8");
  response.end(JSON.stringify(body));
}

async function readRequestBody(request: import("node:http").IncomingMessage) {
  const chunks: Buffer[] = [];
  let size = 0;
  for await (const chunk of request) {
    const buffer = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk);
    size += buffer.length;
    if (size > MAX_PROJECT_BYTES) throw new Error("Project data exceeds 25 MB");
    chunks.push(buffer);
  }
  return Buffer.concat(chunks).toString("utf8");
}

function isProjectPayload(value: unknown): value is Record<string, unknown> {
  if (!value || typeof value !== "object") return false;
  const project = value as Record<string, unknown>;
  return (
    Number.isInteger(project.schemaVersion) &&
    Array.isArray(project.roles) &&
    Array.isArray(project.blocks) &&
    Array.isArray(project.folders) &&
    Array.isArray(project.tests)
  );
}

export function localProjectFile(): Plugin {
  return {
    name: "voxelbench-local-project-file",
    apply: "serve",
    configureServer(server) {
      server.middlewares.use(async (request, response, next) => {
        const url = new URL(request.url ?? "/", "http://localhost");
        if (url.pathname !== ENDPOINT) {
          next();
          return;
        }

        if (request.method === "GET") {
          try {
            const project = await readFile(PROJECT_FILE, "utf8");
            response.statusCode = 200;
            response.setHeader("Cache-Control", "no-store");
            response.setHeader("Content-Type", "application/json; charset=utf-8");
            response.end(project);
          } catch (error) {
            if ((error as NodeJS.ErrnoException).code === "ENOENT") {
              sendJson(response, 404, { error: "No repo-backed project exists yet" });
              return;
            }
            sendJson(response, 500, { error: "Could not read repo-backed project" });
          }
          return;
        }

        if (request.method === "PUT") {
          try {
            const body = await readRequestBody(request);
            const project: unknown = JSON.parse(body);
            if (!isProjectPayload(project)) {
              sendJson(response, 400, { error: "Invalid VoxelBench project data" });
              return;
            }
            await mkdir(dirname(PROJECT_FILE), { recursive: true });
            const temporaryFile = `${PROJECT_FILE}.tmp`;
            await writeFile(temporaryFile, `${JSON.stringify(project, null, 2)}\n`, "utf8");
            await rename(temporaryFile, PROJECT_FILE);
            sendJson(response, 200, {
              path: "project-data/voxelbench-project.json",
              saved: true,
              tests: project.tests.length,
            });
          } catch (error) {
            sendJson(response, 500, {
              error: error instanceof Error ? error.message : "Could not save repo-backed project",
            });
          }
          return;
        }

        response.setHeader("Allow", "GET, PUT");
        sendJson(response, 405, { error: "Method not allowed" });
      });
    },
  };
}
