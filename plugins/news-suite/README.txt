CyberDesktop News Suite 1.0.0

资讯阅读 / Daily reader — 2×4, 4×4
11 built-in public RSS/Atom sources: BBC World, The Guardian, The Verge,
Ars Technica, Deutsche Welle (German), NHK (Japanese), IT之家, Solidot,
少数派, 中国新闻网, 阮一峰的网络日志. Hacker News uses its official Firebase API.
Choose Custom and enter one HTTP(S) RSS/Atom URL per line, up to six feeds.
Custom feeds are interleaved and deduplicated, up to 30 entries per refresh.
HN supports Top / New / Best and fetches up to 24 stories, through the shared host service.

开源热榜 / Open source — 2×4, 4×4
GitHub Trending day/week/month and 14 programming-language filters.
Reads the public Trending HTML using Lexbor. This is not an official JSON API.
When GitHub is unavailable, rate-limits requests, changes its markup, or returns
no repositories for a filter, the last successful result is retained as cached.

Click an item to open its original URL. HN replies open its discussion page.
Click a star to save/remove an entry; the header star toggles saved items.
Mouse wheel or footer arrows turn pages. Dragging never opens a link.
Saved entries (60), read markers (200) and the last snapshot persist per card.
No background refresh when hidden. The content.feeds host service shares identical queries; requests run off the UI thread with timeout,
cancellation, bounded size, automatic retry/backoff and a 15-second manual
refresh cooldown. Windows static system proxy/bypass and libcurl proxy environment
variables are honored; PAC/WPAD scripts are not evaluated. No third-party data
proxy service, API key, login, full article scraping or translation is required.
Availability and content remain subject to each source's own service and terms.

Card UI: system / English / 简体中文. Titles retain their original language;
DirectWrite provides Unicode fallback and bidirectional text shaping.
Light/dark/follow-theme, optional transparent background, adjustable corner radius.
Default refresh: reader 20 minutes, GitHub 30 minutes; configurable 5–180 minutes.

Build: ./build-news.ps1 -Jobs 10
Public preview uses deterministic SDK data. Live acquisition belongs to the host.
Requires Windows x64 MSVC /MT, exported CyberDesktop SDK and static vcpkg
curl and nlohmann-json 3.12. HTTP, XML and HTML parsing are host responsibilities.
The public dependency libraries/content-model supplies only URL/text helpers.
Bundled third-party license texts are in licenses/.

Sources / attribution:
https://github.com/HackerNews/API
https://github.com/trending
https://feeds.bbci.co.uk/news/world/rss.xml
https://www.theguardian.com/world/rss
https://www.theverge.com/rss/index.xml
https://feeds.arstechnica.com/arstechnica/index
https://rss.dw.com/rdf/rss-de-all
https://www3.nhk.or.jp/rss/news/cat0.xml
https://www.ithome.com/rss/
https://www.solidot.org/index.rss
https://sspai.com/feed
https://www.chinanews.com.cn/rss/importnews.xml
https://www.ruanyifeng.com/blog/atom.xml

Package previews use fictional editorial text and repository names, not live news.
No generated bitmap assets are used; previews are exported by the native renderer.

Buttons use shared CyberCanvas hit testing, drag cancellation and transient hover/press feedback.
Tweeny 3.2.1 easing uses MIT; see licenses/Tweeny-MIT.txt.

News controls use CyberCanvas 0.5 with shared themes, feedback and Lucide icons from the optional host service. Older hosts and standalone previews retain text fallbacks.
