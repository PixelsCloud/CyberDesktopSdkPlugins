#include "plugins/news-suite/tools/Samples.h"

namespace news {
Model sample(Kind kind, bool zh) {
    Model m;
    m.loading = false;
    m.fetched = wallSeconds();
    const char* titlesEn[]{"A quieter internet, built around the people who use it",
                           "The small teams making open science possible",
                           "What a city can learn from its trees",
                           "Inside the next generation of personal computing",
                           "Why the best ideas often begin with a small experiment",
                           "A new chapter for independent developers"};
    const char* titlesZh[]{"让技术回归生活：更轻、更安静的数字日常", "开源社区的一周：从小工具到大想法",
                           "城市里的绿色，正在悄悄改变我们的生活",   "属于每个人的创作工具，开始变得简单",
                           "把好奇心留给世界，把时间留给自己",       "独立开发者的新一页，从一个小想法开始"};
    const char* repos[]{"open-garden / paperplane", "little-lab / aurora",  "northstar / pocket-reader",
                        "cloudpath / moss",         "studio / quiet-tools", "makers / everyday"};
    const char* langs[]{"TypeScript", "Python", "Rust", "Go", "JavaScript", "C++"};
    const char* stars[]{"24,618", "18,392", "9,817", "7,526", "6,104", "5,239"};
    const char* growth[]{"1,248", "836", "524", "317", "248", "196"};
    for (int i = 0; i < 6; ++i) {
        Item v;
        v.title = kind == Kind::Github ? repos[i] : zh ? titlesZh[i] : titlesEn[i];
        v.url = kind == Kind::Github ? "https://github.com" : "https://example.org/story/" + std::to_string(i);
        v.summary =
            kind == Kind::Github
                ? (zh ? "为好奇的人打造的小而美的开源工具，让日常创作更轻松。"
                      : "Thoughtful open source tools for curious people. Make something useful, one small idea at a "
                        "time.")
                : (zh ? "从一个小小的想法出发，发现技术、设计与日常生活之间更多有趣的连接。"
                      : "Exploring the ideas connecting technology, design and everyday life, one story at a time.");
        v.source = zh ? "少数派" : "The Guardian";
        v.published = wallSeconds() - (i + 1) * 1200;
        if (kind == Kind::Github) {
            v.language = langs[i];
            v.stars = stars[i];
            v.growth = growth[i];
            v.url += "/sample/repository-" + std::to_string(i);
        }
        m.items.push_back(std::move(v));
    }
    m.saved.push_back(m.items[1]);
    return m;
}
}  // namespace news
