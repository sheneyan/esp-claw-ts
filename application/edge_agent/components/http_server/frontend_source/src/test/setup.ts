// Keep this non-literal so TypeScript does not load jest-dom's Vitest 4
// declaration merge while the project is on Vitest 5. The runtime matcher
// registration remains the same.
const jestDomVitestModule = '@testing-library/jest-dom/vitest';
await import(jestDomVitestModule);

export {};
