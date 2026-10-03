# Third-party notices and distribution checks

## InpOut x64 helper

`resources/inpoutx64.dll` is embedded in the executable as a resource and extracted at runtime to access the SMU mailbox. It was checked against the x64 DLL in the InpOutBinaries_1501 archive in the user's Downloads folder. The archive's `license.txt` identifies the standard MIT License and is preserved at [`resources/inpout-license.txt`](resources/inpout-license.txt).

The DLL's embedded file/product version remains 1.5.0.0 even in the 1.5.0.1 package; the local file is byte-identical to that package's copy. SHA-256:

```text
5F27ED4D5CD58A1EE23DEEB802E09E73F3A1D884CE2135F6E827F67B171269E7
```

The upstream InpOut download page states that v1.5.0.1 updated the license terms to the standard MIT License and includes `License.txt`. The v1.5.0.1 archive preserves the DLL with version metadata 1.5.0.0; its SHA-256 matches the previously used DLL, so no helper code change was introduced. Include the MIT notice with source and binary distributions.

Upstream information: <https://www.highrez.uk/downloads/inpout32/default.htm>

## AMD ADLX

The application loads ADLX from the AMD driver installation at runtime. This repository does not bundle the ADLX SDK or runtime. Review AMD's applicable terms before redistributing any AMD-provided files.
