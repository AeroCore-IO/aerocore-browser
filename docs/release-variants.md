# Release variants

The public repository defaults to the international release source in
`config/release-source.json`. Packaged releases replace it with the selected
variant's profile and produce two installer AppImages from one source tree:

| Variant | Initial home sites and bookmarks | Update API |
| --- | --- | --- |
| `zh-CN` | Chinese sites | `https://api.mirror.aerocore.com.cn` |
| `international` | International sites | `https://api.github.com` |

Run `make VERSION=0.2.0 PACKAGE_VARIANT=zh-CN package-appimage` or use
`PACKAGE_VARIANT=international`. The tagged release workflow builds both and
uploads each AppImage, runtime archive, and SHA-256 file. It also publishes a
legacy Chinese archive under the old filename so existing installations can
update to the variant-aware release.

Edit `packaging/<variant>/distribution.json` to change the initial sites,
bookmarks, or website language preference. Edit
`packaging/<variant>/release-source.json` to change its update API or archive
template. Packaging installs these files as `config/distribution.json` and
`config/release-source.json`. The updater only accepts an archive whose
`manifest.yaml` has the same variant as the installed package.

Each variant's `home_sites` and `bookmarks` arrays define its local defaults.
Initial sites are imported only when the site's database is empty, and initial
bookmarks only when there is no saved bookmark array. Users can manage them
with the same controls as other entries. Updates therefore preserve user
changes rather than replacing their home page.

Both variants use the same installation path. Installing the other variant
over an existing one intentionally switches the installed update source, but
preserves the user's home site and bookmark data.
