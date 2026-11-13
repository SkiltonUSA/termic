// Opt-in hardware-toolchain acceptance. No Amiga hardware access or deployment.
// TERMIC_E2E_AMIGA=1 npm run test:e2e -- --spec e2e/specs/amiga.e2e.ts
// Requires the linked Amiga workspace and its running compiler container VM.
import fs from "node:fs";
import path from "node:path";
import {
  archiveTask, clickMenuItem, openTask, requireTermicApi, snap, waitForAppShell, waitForText,
} from "../helpers";

const root = process.cwd();
const fixture = process.env.E2E_FIXTURE ?? path.join(root, ".e2e", "fixture-repo");
const configPath = path.join(fixture, ".termic.yaml");
const output = path.join(root, ".context", "amiga-demo", "TermicDemo");
const quote = (s: string) => "'" + s.replace(/'/g, "'\\''") + "'";

(process.env.TERMIC_E2E_AMIGA === "1" ? describe : describe.skip)("Amiga build controls", () => {
  let taskId: string;
  let savedConfig: Buffer | undefined;
  let savedBinary: Buffer | undefined;

  before(async () => {
    await waitForAppShell();
    await requireTermicApi();
    if (fs.existsSync(configPath)) savedConfig = fs.readFileSync(configPath);
    if (fs.existsSync(output)) savedBinary = fs.readFileSync(output);
    // Use the shipped command labels and configuration, adjusting only the
    // launcher path because the UI runs inside the isolated fixture repository.
    const yaml = fs.readFileSync(path.join(root, "scripts/amiga/termic.yaml"), "utf8")
      .replaceAll("python3 scripts/amiga/workspace.py", `python3 ${quote(path.join(root, "scripts/amiga/workspace.py"))}`);
    fs.writeFileSync(configPath, yaml);
    taskId = await openTask("Amiga development");
  });

  after(async () => {
    try {
      if (taskId) await archiveTask(taskId);
    } finally {
      if (savedConfig) fs.writeFileSync(configPath, savedConfig);
      else fs.rmSync(configPath, { force: true });
      // Preserve the last successful binary if compilation failed midway.
      if (!fs.existsSync(output) && savedBinary) fs.writeFileSync(output, savedBinary);
    }
  });

  async function chooseCommand(label: string) {
    const opened = await browser.execute(() => {
      const run = [...document.querySelectorAll("button")].find(
        b => b.textContent?.trim() === "Run" && b.getBoundingClientRect().width > 0,
      );
      const menu = run?.nextElementSibling;
      if (!(menu instanceof HTMLButtonElement) || menu.getAttribute("aria-haspopup") !== "menu") return false;
      menu.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true, button: 0, pointerType: "mouse" }));
      menu.dispatchEvent(new PointerEvent("pointerup", { bubbles: true, button: 0, pointerType: "mouse" }));
      return true;
    });
    expect(opened).toBe(true);
    await waitForText(label);
    await clickMenuItem(label);
  }

  async function waitForSuccessfulRun(label: string) {
    const id = (await browser.waitUntil(async () => browser.execute((task, member) => {
      return (window.__termic!.useApp.getState().tabs[task] ?? [])
        .find((t: any) => t.runTab?.member === member)?.id;
    }, taskId, `label:${label}`), { timeout: 15_000, timeoutMsg: `${label}: no run tab` })) as string;
    // Canvas-rendered PTY output has no DOM text. Use the output/exit state,
    // then assert the visible tab title and the compiler's actual artifact.
    await browser.waitUntil(async () => browser.execute((task, tab) => {
      const t = (window.__termic!.useApp.getState().tabs[task] ?? [])
        .find((t: any) => t.id === tab);
      return !!t?.firstOutputAt && !t?.ptyId && !t?.runTab?.failed;
    }, taskId, id), { timeout: 120_000, timeoutMsg: `${label}: did not finish successfully` });
    const title = await browser.execute((tab) =>
      document.querySelector(`[data-tab-id="${tab}"]`)?.textContent, id);
    expect(title).toContain(label);

  }

  it("runs the environment check from the dropdown", async () => {
    await chooseCommand("Amiga environment check");
    await waitForSuccessfulRun("Amiga environment check");
    await snap("amiga-environment.png");
  });

  it("builds a fresh native Workbench executable from the dropdown", async () => {
    fs.rmSync(output, { force: true });
    await chooseCommand("Build Workbench demo");
    await waitForSuccessfulRun("Build Workbench demo");
    expect(fs.readFileSync(output).subarray(0, 4).toString("hex")).toBe("000003f3");
    await snap("amiga-build.png");
  });
});
