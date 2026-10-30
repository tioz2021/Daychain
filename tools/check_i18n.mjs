// Compare the key sets of the three UI dictionaries.
import { readFileSync } from "node:fs";

const files = {
  en: "web/i18n/en.js",
  ru: "web/i18n/ru.js",
  uk: "web/i18n/uk.js",
};

const sets = {};
for (const [lang, path] of Object.entries(files)) {
  const src = readFileSync(path, "utf8");
  sets[lang] = new Set([...src.matchAll(/^ {2}([A-Za-z0-9_]+):/gm)].map((m) => m[1]));
}

const base = sets.en;
let bad = 0;
for (const lang of ["ru", "uk"]) {
  for (const key of base) {
    if (!sets[lang].has(key)) {
      console.error(`missing in ${lang}: ${key}`);
      bad++;
    }
  }
  for (const key of sets[lang]) {
    if (!base.has(key)) {
      console.error(`extra in ${lang}: ${key}`);
      bad++;
    }
  }
}
if (bad) {
  console.error(`${bad} i18n key mismatches`);
  process.exit(1);
}
console.log(`i18n ok: ${base.size} keys in 3 languages`);
