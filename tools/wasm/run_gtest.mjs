import { pathToFileURL } from "node:url";

const [program, ...arguments_] = process.argv.slice(2);
const { default: createModule } = await import(pathToFileURL(program));
await createModule({ arguments: arguments_ });
