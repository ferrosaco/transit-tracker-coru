import { createServer } from "node:http";
import { readFileSync } from "node:fs";
import { routesForStop, scheduleFor, searchStops } from "./itranvias.mjs";

const html = readFileSync(new URL("./index.html", import.meta.url));

const server = createServer(async (request, response) => {
  const url = new URL(request.url || "/", "http://127.0.0.1");
  try {
    if (request.method === "GET" && url.pathname === "/") {
      response.writeHead(200, { "content-type": "text/html; charset=utf-8" });
      response.end(html);
      return;
    }
    if (request.method === "GET" && url.pathname === "/stops") {
      return send(response, 200, await searchStops(url.searchParams.get("q") || ""));
    }
    const routesMatch = url.pathname.match(/^\/stops\/([^/]+)\/routes$/);
    if (request.method === "GET" && routesMatch) {
      const routes = await routesForStop(decodeURIComponent(routesMatch[1]));
      if (!routes) return send(response, 404, { message: "Parada no encontrada" });
      return send(response, 200, routes);
    }
    if (request.method === "GET" && url.pathname.startsWith("/schedule/")) {
      const pairs = decodeURIComponent(url.pathname.slice("/schedule/".length));
      const limit = Math.min(Number(url.searchParams.get("limit") || 8), 10);
      return send(response, 200, { trips: await scheduleFor(pairs, limit) });
    }
    send(response, 404, { message: "No encontrado" });
  } catch (error) {
    console.error(error);
    send(response, 502, { message: "No se han podido leer los buses de iTranvías" });
  }
});

function send(response, status, body) {
  response.writeHead(status, { "content-type": "application/json; charset=utf-8" });
  response.end(JSON.stringify(body));
}

const port = Number(process.env.PORT || 8080);
server.listen(port, "0.0.0.0", () => {
  console.log("Abre http://127.0.0.1:" + port);
});
