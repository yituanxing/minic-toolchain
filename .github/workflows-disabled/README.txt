Historical CI evidence, not active GitHub Actions entrypoints.

The complete pre-cleanup archive (321 YAML files) is preserved at immutable commit
de72e2982e9576ec36a8110a371287300ed29937:
https://github.com/yituanxing/minic-toolchain/tree/de72e2982e9576ec36a8110a371287300ed29937/.github/workflows-disabled

Historical reports may cite a retired filename. Resolve it at that commit, not
against the current checkout. The remaining local YAMLs are conservatively
retained for literal references from active code/checks or the MiniPP wildcard.
Do not remove those files without preserving the exact historical validation
contract. An archived job is not current execution evidence.

The completed 20/120-object Kbuild transfer experiment bundles are preserved at
https://github.com/yituanxing/minic-toolchain/tree/4b6048d5de48540c9e977d58e1e2c76fd757f341
The local 120-object definition is retained only for the existing blob guard;
its historical inputs are recoverable from that immutable tree.
