# Code signing, VST branding, and licence notices for Forte releases

Sources were checked on 2026-11-14. Each claim links to the source it came from. Where a page shows a revision date, that date is given. Anything not backed by a primary source is marked **(inference)** or **(unverified)**.

## TL;DR (plain language)

**Signing:** Apply to **SignPath Foundation** for free OSS signing (it has a first-class GitHub Actions integration). But two of its rules may block a brand-new solo project: it needs "a certain verifiable reputation" for executables, and it rules out "commercial dual-licensing for all components", which JUCE has. Ask SignPath about both before relying on it. If you live in the **US or Canada**, **Microsoft Artifact Signing** (formerly Trusted Signing) is the easy paid route: about US$9.99/month, no hardware token, signs straight from GitHub Actions. Everywhere else, the cheapest real certificate is **Certum's Open Source Code Signing** set at about €69. It needs a smart card, so it doesn't work in CI. Until one of these is in place, **shipping unsigned is workable** for a niche free plugin. Users click through one SmartScreen "Run anyway" prompt on the installer. Hosts don't check signatures. The real casualty is **Smart App Control**: where it is in enforcement mode, it blocks the unsigned installer and the unsigned `.vst3` DLL, with no override. No certificate type (OV, EV or Artifact Signing) gives instant SmartScreen trust any more. Reputation always has to build up.

**VST name/logo:** You don't sign anything. Using "VST" is optional, but if you use the word at all, Steinberg's guidelines require you to show the **VST Compatible logo** (it ships in the SDK under `VST_SDK/VST3_SDK/vst3_doc/artwork`) next to the claim, on every web page that promotes the product, and in docs and the About box. Write "VST®" on first use and include the exact line **"VST is a registered trademark of Steinberg Media Technologies GmbH."** Don't use "VSTi" or any other variant.

**Notices:** Ship the GPLv3 and AGPLv3 texts and Corresponding Source (for example the tagged GitHub source). Ship the MIT notice for the VST3 SDK. Ship the copyright line and OFL text for Jost and Italiana. Ship notices for whichever JUCE-vendored libraries actually end up in the binary. Show them in the About box and in a `THIRD_PARTY_NOTICES` file.

---

## 1. Windows code signing options (Nov 2026)

### 1a. SignPath Foundation (free for OSS)

What it is: SignPath Foundation gives OSS projects a free code-signing certificate, and SignPath.io runs the signing service. "For OSS projects, our services are free of charge." ([signpath.org](https://signpath.org/)). The certificate is **issued to SignPath Foundation, not to you**: "The code signing certificate is issued to SignPath Foundation". So Windows dialogs show SignPath Foundation as the publisher ([SignPath Foundation terms](https://signpath.org/terms), no revision date; the code-of-conduct section is labelled draft). There's no personal ID check ("No need for personal identification", [signpath.org](https://signpath.org/)).

Eligibility, from the [terms](https://signpath.org/terms):
- **Licence:** "The project must use an OSI-approved Open Source license without commercial dual-licensing for all components." Also: "The project may not contain any proprietary, non open-source component (especially code published by a maintainer or an affiliated person/organization)."
  - **Risk for Forte (inference):** JUCE is offered under "AGPLv3 and the commercial JUCE licence" ([JUCE LICENSE.md](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md)). Read literally, "without commercial dual-licensing for all components" could exclude any JUCE-based project. The bracketed example suggests the rule really targets *maintainers* who dual-license their own code, which Forte doesn't do. **Ask SignPath before applying.**
- **Maturity:** "The project must be actively maintained." "The project must already be released in the form that should be signed." Functionality must be documented on the download page.
- **Reputation:** "For executable programs that may be downloaded and executed based on our signature, we require a certain verifiable reputation. (Not for developer libraries/components/packages though.)" No threshold is published. SignPath checks this once, at onboarding ("we don't repeat these verifications periodically").
- **Team:** The signing team must be the development team and must own the repo. The terms define the roles Authors, Reviewers and Approvers, and every release needs manual approval. They don't say whether one person may hold all the roles. **(unverified for solo projects: ask.)**
- **Security:** MFA is required on both SignPath and GitHub.
- **Build:** Binaries "must be built from source code in a verifiable way". SignPath checks that each signed file is "a valid, automated build" from the repo.
- **Website:** You must publish a "Code signing policy" page with the line "Free code signing provided by SignPath.io, certificate by SignPath Foundation", the team roles, and a privacy statement.
- **Content:** No malware or PUA. Software must not change system configuration without warning. Anything with an installer must offer an uninstaller.

GitHub Actions integration ([SignPath docs: GitHub](https://docs.signpath.io/trusted-build-systems/github)): use `signpath/github-action-submit-signing-request@v3` after `actions/upload-artifact` v4+. For OSS projects, SignPath verifies that "All jobs of the GitHub workflow leading up to the signing request were executed on GitHub-hosted agents". Origin metadata "is provided by GitHub, not the build script, and can therefore not be forged."

**Does a solo GPL project qualify?** GPLv3 meets the licence rule. Being solo isn't explicitly ruled out. The open questions are (1) the JUCE dual-licence clause and (2) "verifiable reputation" for a new executable. Apply once there are public releases and some users.

### 1b. Microsoft Artifact Signing (formerly Trusted Signing)

- **Name:** Microsoft has renamed Trusted Signing to Artifact Signing ([SmartScreen reputation doc](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation), ms.date 2026-05-04, updated 2026-08-17).
- **Price:** "Starts at $9.99/month" (same doc). The [pricing page](https://azure.microsoft.com/en-us/pricing/details/artifact-signing/) lists Basic with 5,000 signatures a month and 1 profile of each type, and Premium with 100,000 a month. Its price cells didn't render when fetched. Premium is widely quoted at $99.99/month **(unverified)**. Billing isn't pro-rated, and you can't use free, trial or sponsored Azure subscriptions: "you must have a paid Azure subscription" ([FAQ](https://learn.microsoft.com/en-us/azure/artifact-signing/faq), ms.date 2026-05-14, updated 2026-10-27).
- **Eligibility by country:** "Public Trust certificates are available to organizations in the United States, Canada, the European Union, the United Kingdom, Australia, New Zealand, Japan, South Korea, Singapore, Switzerland, Norway, and Israel. **Individual developers must be located in the United States or Canada.**" ([Quickstart](https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart), ms.date 2026-05-21, updated 2026-10-08).
- **Identity validation for individuals:** The Azure billing account must have Account Type "Individual". Its legal name and sold-to address must match a government-issued ID. You verify through Microsoft Entra Verified ID with the AU10TIX partner, which involves a document scan, a Face Check and the Microsoft Authenticator app. The certificate shows your name plus city, state and country; email and street address are left out (Quickstart). Validation takes 1 to 20 business days.
- **Certificate:** No custom CN or O (your legal name is the CN). No EV, "There's no plan to issue EV certificates in the future" (FAQ). You never hold the key, and the service stores it at FIPS 140-3 Level 3. Certificates are short-lived and renew automatically.
- **CI:** No hardware token is needed. It integrates with GitHub Actions and Azure DevOps (SmartScreen doc; [signing integrations](https://learn.microsoft.com/en-us/azure/artifact-signing/how-to-signing-integrations)).

### 1c. Traditional OV/EV certificates

- **Hardware key is mandatory:** Since 2023-06-01 the CA/B Forum Code Signing Baseline Requirements say: "CAs SHALL ensure that the Subscriber's Private Key is generated, stored, and used in a suitable Hardware Crypto Module" ([CA/B Forum CSBR](https://cabforum.org/working-groups/code-signing/requirements/), current version 3.11.0). That means a USB token, a smart card, or a cloud HSM service from the CA. A physical token doesn't work on GitHub-hosted runners unless you pay for the CA's cloud signing service.
- **Shorter lifetime:** "For Code Signing Certificates issued on or after March 1st, 2026, the validity period MUST NOT exceed 460 days" (CSBR). Plan on buying or renewing about every 15 months.
- **Cheapest individual option found:** Certum "Open Source Code Signing" set for €69, which includes a cryptoCertum smart card and reader. The certificate subject is your own name prefixed with "Open Source Developer" ([Certum shop](https://shop.certum.eu/open-source-code-signing.html)). It is aimed at free and open-source developers.
- **Commercial OV/EV:** Prices from the big CAs vary. They commonly run from a few hundred to several hundred US$ a year, with EV higher **(unverified; no primary price list was checked)**.
- **EV no longer helps with SmartScreen:** "EV certificates no longer bypass SmartScreen … Paying a premium for EV solely to avoid SmartScreen warnings is no longer justified." ([SmartScreen doc](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)).

### 1d. Shipping unsigned: what actually happens

**SmartScreen (installer / standalone .exe downloaded from GitHub):**
- Unsigned: "'Windows protected your PC'; User must choose 'Run anyway' before the app can run. Enterprise policy can prevent continuation entirely." ([SmartScreen doc](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)).
- "When a file is not signed, SmartScreen reputation must build for each new version of your files, starting with zero reputation."
- SmartScreen AppRep "only checks the reputation of the entry point program". So a `.vst3` DLL that a DAW loads isn't checked by SmartScreen. Only the installer or `.exe` the user double-clicks is ([Eric Lawrence, Microsoft Defender, 2026-04-28](https://textslashplain.com/2026/04/28/smart-app-control/), an expert blog, not official docs).

**Smart App Control (Windows 11):**
- "Malware, Potentially Unwanted Apps (PUA), and unknown, unsigned code are blocked by default." In enforcement mode, "Apps cannot be run unless they are recognized by Microsoft's app intelligence services, or they are signed with a trusted certificate" ([Smart App Control overview](https://learn.microsoft.com/en-us/windows/apps/develop/smart-app-control/overview), ms.date 2026-09-30).
- "Smart App Control signature checks apply to all executable files, not just those downloaded from the Internet" ([SmartScreen doc](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)).
- DLLs: "If a file isn't properly signed, Smart App Control blocks it. The functionality that depends on the file doesn't work, but the rest of the app may continue to work fine." ([Microsoft Support: "Smart App Control has blocked part of this app"](https://support.microsoft.com/en-US/Windows/Security/Threat-Malware-Protection/smart-app-control-has-blocked-part-of-this-app)). **Effect:** under SAC enforcement, an unsigned `Forte.vst3` would fail to load in a signed DAW, and the user would see "Part of this app has been blocked". The block dialog "does not offer an override" (Lawrence).
- Mitigation: SAC only reaches enforcement on some machines, and it turns itself off for developer-type users during evaluation (SAC overview). It can be re-enabled since Windows 11 25H2 (Lawrence). Signing with any CA in the Trusted Root Program (SignPath, Artifact Signing, Certum) satisfies SAC even before reputation builds (SAC overview).

**DAW hosts:** No primary source was found showing that any major Windows DAW checks Authenticode signatures on VST3 plugins. Windows hosts load unsigned `.vst3` files routinely **(inference; macOS notarisation is a different story and doesn't apply here)**.

### Does SmartScreen reputation still have to be built for OV / Artifact Signing certs?

Yes. A signed file with a valid OV/EV certificate still gets "Warning: app flagged as unrecognized until reputation accumulates; verified publisher name is displayed". "There is no exact threshold, but it can take several weeks and hundreds of clean installs from a wide audience." For Artifact Signing: "reputation accumulates over time based on download volume and behavior". The upside of signing is that reputation can carry over between versions: "Signing files using a trusted certificate can allow certificate reputation to build, potentially avoiding warnings on new files signed by the same trusted certificate" ([SmartScreen doc](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation); [Artifact Signing FAQ](https://learn.microsoft.com/en-us/azure/artifact-signing/faq)). A SignPath Foundation certificate is shared across many OSS projects, so it may already carry publisher reputation **(inference)**.

### Recommendation (1)

1. **v0.x releases:** ship unsigned. Explain the SmartScreen "More info, then Run anyway" step and the SAC limitation in the README. Also offer a zip of the bare `.vst3`, because SmartScreen doesn't check a DLL that a host loads.
2. **Once there are public releases and some users:** email SignPath Foundation. Ask about the JUCE dual-licence clause and the solo-team role rule, then apply. If accepted, add the GitHub Action and the "Code signing policy" page. This is the best result: free, automated CI signing.
3. **Fallback:** if you're in the US or Canada, use Artifact Signing (about $9.99/month, GitHub Actions, your name on the cert). Otherwise use Certum Open Source (€69, with local signing on a smart card at release time).
4. Skip EV entirely.

---

## 2. Steinberg VST trademark and logo rules (VST3 SDK under MIT)

- **SDK licence:** "Since version 3.8 VST 3 is licensed under MIT license." "Mostly all files are under MIT license, except VSTGUI and mda files which are under BSD-like license." ([VST 3 Licensing](https://steinbergmedia.github.io/vst3_dev_portal/pages/VST+3+Licensing/Index.html)). The latest release is 3.8.1, committed 2026-08-11 ([vst3sdk](https://github.com/steinbergmedia/vst3sdk)). The licence file reads "Copyright (c) 2026, Steinberg Media Technologies GmbH" ([LICENSE.txt](https://github.com/steinbergmedia/vst3sdk/blob/master/LICENSE.txt)).
- **No agreement needed:** "Trademark usage (e.g. "VST" name or logo) is optional under MIT license, but if used, must comply with Steinberg's official trademark rules." Neither the licensing page nor the guidelines mention a signed agreement. The old VST usage-licence agreement belonged to the VST 2 / dual-licence era ([Licensing FAQ](https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Licensing.html)).
- **Where the rules live:** [Steinberg VST usage guidelines](https://steinbergmedia.github.io/vst3_dev_portal/pages/VST+3+Licensing/Usage+guidelines.html) on the VST 3 Developer Portal (the page footer says © 2026; contact reception@steinberg.de). The logo files ship in the SDK at `VST_SDK/VST3_SDK/vst3_doc/artwork`.

Key rules, quoted from the usage guidelines:
- **Logo goes with the word:** If you "display the VST Compatible Logo or refer to the VST trademark", the logo is required on packaging, websites, docs, marketing, and the About box (which "should" show it, or the attribution text if space is short). Using "VST" in standard font is allowed, but "In all cases, the VST Compatible Logo must appear in relevant context, adjacent to or associated with the use of the term 'VST'." **So the logo is effectively required as soon as you say "VST3 plugin".**
- **Websites:** "must be visible on every web page referencing or promoting a product using or created with the VST SDK. Displaying the VST Compatible Logo only in a site-wide footer or imprint is not sufficient." This covers the GitHub README and the Releases page **(inference)**.
- **Compatibility claims:** These are allowed if the product "was developed using the VST SDK" and the logo "is shown near the claim, on the same page or surface."
- **Product names:** "MyCoolEngine for VST" style is OK in plain text with the logo close by. You can't stylise "VST" or merge it into the brand. No "VST" in company names or company logos. "It is not permitted to create modified terms or compositions such as 'VSTi'."
- **Logo size:** There are two minimum printing sizes, 20 x 18.7 mm and 15 x 10 mm. "These dimensions must be respected in all digital and printed uses."
- **Required notice (section 15):** "The first use of 'VST' in any product-related material must include the ® symbol. Additionally, the following attribution must appear in all product credits and documentation: **'VST is a registered trademark of Steinberg Media Technologies GmbH.'**"

### Recommendation (2)

Use the term and follow the rules, because "VST3 plugin" is how users find it. Copy the logo from the SDK's `vst3_doc/artwork` into the repo. Put the logo plus the notice line in the README (near the "VST3" mention), in release notes and on any project site, and in the plugin's About panel. Write "VST®3" or "VST® 3" on first mention. Never write "VSTi". Keep "VST" out of the product name ("Forte", not "Forte VST"). If you'd rather not deal with the logo, you can't use the word "VST" at all. Plain descriptions such as "plugin for Windows DAWs" would have to do, which is a poor trade.

---

## 3. Notice and attribution obligations for the shipped binary

**JUCE (AGPLv3 option), combined with Forte (GPLv3-or-later):**
- JUCE modules are "AGPL-3.0-only OR LicenseRef-JUCE-Commercial" ([JUCE.spdx.json](https://github.com/juce-framework/JUCE/blob/master/JUCE.spdx.json)). GPLv3 §13 allows combining with AGPLv3 code. The AGPL terms then apply to the combined work for the JUCE parts ([GPLv3](https://www.gnu.org/licenses/gpl-3.0.html) §13; [AGPLv3](https://www.gnu.org/licenses/agpl-3.0.html) §13). Because JUCE is "-only", the shipped binary is effectively bound to version 3 (inference).
- Obligations when conveying object code (GPL/AGPL §4–6): keep copyright and licence notices intact. Include the GPLv3 and AGPLv3 texts. Provide the Corresponding Source, including build scripts, for example by linking the exact git tag next to the download (§6(d) allows source "from a network server" with clear directions). For interactive UIs, show "Appropriate Legal Notices" (§5(d)): copyright, no warranty, licence, and how to see it. An About box covers this. The AGPL network-interaction clause (§13) doesn't matter for a local plugin (inference).
- No JUCE splash screen is required under AGPL (none appears in LICENSE.md).
- **Vendored dependencies:** JUCE compiles in third-party code depending on which modules and options you use. Examples from the SBOM: zlib (Zlib), HarfBuzz (MIT-Modern-Variant), SheenBidi (**Apache-2.0**: include its licence and any NOTICE), libpng, IJG jpeglib, libwebp/FLAC/Ogg/Vorbis/Opus (BSD-3-Clause), LunaSVG/PlutoVG/QuickJS (MIT), and the VST3 SDK (MIT). If you enable `JUCE_ASIO`, the ASIO SDK is "LicenseRef-Steinberg-ASIO OR GPL-3.0-only". The GPL path works for Forte. Generate the notice list from `JUCE.spdx.json`, filtered to the modules you actually link.

**VST3 SDK (MIT):** "You must include the original copyright notice and the MIT license text in all copies or substantial portions of the code" ([Licensing FAQ](https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Licensing.html)). Ship the [LICENSE.txt](https://github.com/steinbergmedia/vst3sdk/blob/master/LICENSE.txt) text ("Copyright (c) 2026, Steinberg Media Technologies GmbH"). JUCE's VST3 wrapper doesn't use VSTGUI. If any mda or VSTGUI file is used, add its BSD-style notice. The trademark notice from section 2 is a separate requirement.

**SIL OFL 1.1 fonts (Jost, Italiana) embedded in the binary:**
- OFL condition 2: fonts "may be bundled, redistributed and/or sold with any software, provided that each copy contains the above copyright notice and this license. These can be included either as stand-alone text files, human-readable headers or in the appropriate machine-readable metadata fields within text or binary files as long as those fields can be easily viewed by the user." The fonts can't be sold by themselves ([OFL 1.1 text](https://openfontlicense.org/open-font-license-official-text/)).
- The OFL FAQ for bundled apps says: "At a minimum you must include the copyright statement, the license notice and the license text. A mention of this information in your About box or Changelog, with a link to where the font package is from, is good practice" ([OFL FAQ](https://openfontlicense.org/ofl-faq/)).
- Copyright lines (from the Google Fonts repo):
  - Jost: "Copyright 2020 The Jost Project Authors (https://github.com/indestructible-type)" ([OFL.txt](https://github.com/google/fonts/blob/main/ofl/jost/OFL.txt))
  - Italiana: "Copyright (c) 2011, Santiago Orozco (hi@typemade.mx), with Reserved Font Name "Italiana"." ([OFL.txt](https://github.com/google/fonts/blob/main/ofl/italiana/OFL.txt))
- **Reserved Font Name caveat:** Italiana has an RFN. The OFL FAQ treats subsetting or optimising a font as creating a Modified Version, which "would not normally allow the use of RFNs". **Embed Italiana unmodified** (don't subset it), or rename any subset. Jost has no RFN.

**Practical checklist:** an About panel listing the Forte copyright and GPL, the no-warranty line, a source link, "VST is a registered trademark…", the VST logo, and credits for JUCE, the VST3 SDK, Jost and Italiana. Also ship `LICENSE` (GPLv3), `LICENSES/AGPL-3.0.txt`, and a `THIRD_PARTY_NOTICES.txt` (VST3 MIT, OFL x2 with copyright lines, the JUCE-vendored libraries actually linked, including Apache-2.0 SheenBidi) inside the installer or zip and in the repo.

## Open questions

- SignPath Foundation: does JUCE's commercial dual licence break the "no commercial dual-licensing for all components" rule? Can one person fill the Author, Reviewer and Approver roles? How much reputation counts as enough? Email them before applying.
- The exact Artifact Signing Premium price (the pricing page didn't render its prices).
- Which JUCE-vendored libraries end up in Forte's binary depends on the final module list. Re-check `JUCE.spdx.json` at release time.
