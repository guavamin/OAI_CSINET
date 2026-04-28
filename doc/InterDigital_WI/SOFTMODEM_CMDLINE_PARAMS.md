# Softmodem Command-Line Params and Check Array

This note documents the coupling between `CMDLINE_PARAMS_DESC` and `CMDLINE_PARAMS_CHECK_DESC` in `executables/softmodem-common.h`, and how to add or change params safely.

## The two arrays

- **`CMDLINE_PARAMS_DESC`**: defines the list of common command-line parameters (e.g. `--rfsim`, `--nfapi`, `--imscope-windows`) and where their values are stored.
- **`CMDLINE_PARAMS_CHECK_DESC`**: defines, for each param, an optional *check* function. The config layer calls this after reading a value (e.g. to validate or map a string to an integer).

In `softmodem-common.c`, `config_set_checkfunctions()` assigns `params[i].chkPptr = &cmdline_CheckParams[i]`, and later `config_execcheck()` calls `params[i].chkPptr->s4.f4(cfg, &params[i])` when the check is non-NULL. So **entry index `i` in the check array is used for param index `i`**.

## What can go wrong

1. **Different sizes**  
   A `static_assert` in `get_common_options()` enforces `sizeof(cmdline_params) == sizeof(cmdline_CheckParams)`. If you add a new param to `CMDLINE_PARAMS_DESC` but forget to add a matching entry to `CMDLINE_PARAMS_CHECK_DESC` (or the opposite), the build fails with:
   ```text
   static assertion failed: "cmdline_params and cmdline_CheckParams should have the same size"
   ```

2. **Wrong order**  
   If the number of entries matches but the **order** of check entries does not match the order of params, the wrong check runs for a param. For example, the only param that uses a non-NULL check here is **"nfapi"** (string → integer via `config_checkstr_assign_integer`). If the `.s3a` block is placed at the wrong index, another param (e.g. an integer/boolean like "non-stop") gets that check. Then `config_checkstr_assign_integer` runs with a non-string value and can segfault in `strcasecmp()` (e.g. NULL or invalid pointer).

## Rule when adding a new param

1. Add **one** new entry to `CMDLINE_PARAMS_DESC` (in the desired position).
2. Add **exactly one** new entry to `CMDLINE_PARAMS_CHECK_DESC` **at the same index**:
   - For params that need no special check: use `{ .s5 = { NULL } }`.
   - For a param that needs a string→integer or custom check: use the appropriate `checkedparam_t` member (e.g. `.s3a` for string-to-integer mapping) **in the same position as the param**.

## Current layout (for reference)

- Params 0–24: no check → `{ .s5 = { NULL } }`.
- Param 25 (**"nfapi"**): string check → `{ .s3a = { config_checkstr_assign_integer, ... } }`.
- Params 26–37: no check → `{ .s5 = { NULL } }`.

So when adding a param **before** "nfapi", you add one `{ .s5 = { NULL } }` before the `.s3a` block; when adding **after** "nfapi", add one after it. When adding in the middle of the "no check" runs, add one `.s5` at the same position in the check array.

## See also

- `common/config/config_paramdesc.h`: `checkedparam_t` union and check variants (s1, s3a, s4, s5, etc.).
- `common/config/config_userapi.c`: `config_execcheck()`, `config_set_checkfunctions()`, `config_checkstr_assign_integer()`.
