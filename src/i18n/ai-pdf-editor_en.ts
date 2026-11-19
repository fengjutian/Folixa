<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="en" sourcelanguage="zh_CN">
    <context>
        <name>aipdf::ai::CredentialStore</name>
        <message>
            <source>写入 Windows 凭据管理器失败（错误码 %1）</source>
            <translation>Failed to write to Windows Credential Manager (error code %1)</translation>
        </message>
        <message>
            <source>凭据名称为空</source>
            <translation>The credential name is empty</translation>
        </message>
        <message>
            <source>删除 Windows 凭据失败（错误码 %1）</source>
            <translation>Failed to delete the Windows credential (error code %1)</translation>
        </message>
        <message>
            <source>当前系统不支持 Windows 凭据管理器</source>
            <translation>Windows Credential Manager is not supported on this system</translation>
        </message>
    </context>
    <context>
        <name>aipdf::ai::DataPolicy</name>
        <message>
            <source>一次最多发送 %1 块，本次 %2 块</source>
            <translation>At most %1 chunks can be sent per request; this request has %2</translation>
        </message>
        <message>
            <source>一次最多发送 %1 字，本次 %2 字</source>
            <translation>At most %1 characters can be sent per request; this request has %2</translation>
        </message>
        <message>
            <source>本地模式：文档内容不会离开本机</source>
            <translation>Local mode: document content never leaves this machine</translation>
        </message>
        <message>
            <source>没有可发送的内容</source>
            <translation>There is nothing to send</translation>
        </message>
        <message>
            <source>选定页面模式需要先勾选要发送的页面</source>
            <translation>Selected-pages mode requires choosing the pages to send first</translation>
        </message>
    </context>
    <context>
        <name>aipdf::ai::MockProvider</name>
        <message>
            <source>· 没有检索到可引用的材料。</source>
            <translation>· No citable material was found.</translation>
        </message>
        <message>
            <source>· 第 %1 页的材料说：%2 [%3]</source>
            <translation>· The material on page %1 says: %2 [%3]</translation>
        </message>
        <message>
            <source>【演示模式】下面是按当前页文本拼出的摘要示例（没有真正调用模型）。
· 材料开头：%1
· 材料长度：%2 字
· 要真正的模型摘要，请在设置里填 API 密钥。</source>
            <translation>[Demo mode] Below is a sample summary assembled from the current page text (no model was actually called).
· Material starts with: %1
· Material length: %2 characters
· For a real model summary, enter an API key in Settings.</translation>
        </message>
        <message>
            <source>【演示模式】下面是按检索到的材料拼出的回答示例（没有真正调用模型）。</source>
            <translation>[Demo mode] Below is a sample answer assembled from the retrieved material (no model was actually called).</translation>
        </message>
        <message>
            <source>【演示模式】下面是按选中文字拼出的翻译示例（没有真正调用模型）。
· 原文：%1
· 要真正的模型翻译，请在设置里填 API 密钥。</source>
            <translation>[Demo mode] Below is a sample translation assembled from the selected text (no model was actually called).
· Source text: %1
· For a real model translation, enter an API key in Settings.</translation>
        </message>
        <message>
            <source>【演示模式】下面是按选中文字拼出的解释示例（没有真正调用模型）。
· 选中内容：%1
· 要真正的模型解释，请在设置里填 API 密钥。</source>
            <translation>[Demo mode] Below is a sample explanation assembled from the selected text (no model was actually called).
· Selected text: %1
· For a real model explanation, enter an API key in Settings.</translation>
        </message>
        <message>
            <source>【演示模式】收到你的问题，下面是本地拼出的示例回答（没有真正调用模型）。
· 材料开头：%1
· 材料长度：%2 字</source>
            <translation>[Demo mode] Question received; below is a sample answer assembled locally (no model was actually called).
· Material starts with: %1
· Material length: %2 characters</translation>
        </message>
        <message>
            <source>上一次请求还没结束。</source>
            <translation>The previous request has not finished yet.</translation>
        </message>
        <message>
            <source>演示模式（本地应答，未接入模型）</source>
            <translation>Demo mode (local replies, no model connected)</translation>
        </message>
        <message>
            <source>这一句是演示模式伪造的，原文里并没有它。</source>
            <translation>This sentence was fabricated by demo mode; it is not in the source.</translation>
        </message>
        <message>
            <source>（空）</source>
            <translation>(empty)</translation>
        </message>
    </context>
    <context>
        <name>aipdf::ai::OpenAiProvider</name>
        <message>
            <source>API 密钥无效或已过期，请在设置里重新填写。</source>
            <translation>The API key is invalid or expired. Enter a new one in Settings.</translation>
        </message>
        <message>
            <source>上一次请求还没结束。</source>
            <translation>The previous request has not finished yet.</translation>
        </message>
        <message>
            <source>云端模型（OpenAI 兼容）</source>
            <translation>Cloud model (OpenAI compatible)</translation>
        </message>
        <message>
            <source>模型服务出错了，请稍后再试。</source>
            <translation>The model service returned an error. Try again shortly.</translation>
        </message>
        <message>
            <source>模型请求失败。</source>
            <translation>The model request failed.</translation>
        </message>
        <message>
            <source>没有配置 API 密钥，无法调用云端模型。</source>
            <translation>No API key is configured, so the cloud model cannot be called.</translation>
        </message>
        <message>
            <source>请求太频繁，服务商限流了，请稍后再试。</source>
            <translation>Too many requests; the provider is rate limiting. Try again shortly.</translation>
        </message>
        <message>
            <source>请求被模型服务拒绝：%1</source>
            <translation>The model service rejected the request: %1</translation>
        </message>
        <message>
            <source>请求超时</source>
            <translation>Request timed out</translation>
        </message>
        <message>
            <source>请求超时，服务商没有在时限内回应。</source>
            <translation>The request timed out; the provider did not respond in time.</translation>
        </message>
        <message>
            <source>这个密钥没有权限访问该模型。</source>
            <translation>This key is not allowed to access that model.</translation>
        </message>
        <message>
            <source>连不上模型服务，请检查网络。</source>
            <translation>Cannot reach the model service. Check your network connection.</translation>
        </message>
    </context>
    <context>
        <name>aipdf::ai::ProviderConfig</name>
        <message>
            <source>单价不能是负数</source>
            <translation>The unit price cannot be negative</translation>
        </message>
        <message>
            <source>服务地址不能为空</source>
            <translation>The service URL cannot be empty</translation>
        </message>
        <message>
            <source>服务地址必须以 http:// 或 https:// 开头</source>
            <translation>The service URL must start with http:// or https://</translation>
        </message>
        <message>
            <source>服务地址里没有主机名</source>
            <translation>The service URL has no host name</translation>
        </message>
        <message>
            <source>模型名不能为空</source>
            <translation>The model name cannot be empty</translation>
        </message>
        <message>
            <source>没有选服务商</source>
            <translation>No provider selected</translation>
        </message>
        <message>
            <source>超时时间不能小于 1 秒</source>
            <translation>The timeout cannot be less than 1 second</translation>
        </message>
        <message>
            <source>不认识这个服务商，请重新选择</source>
            <translation>This provider is not recognised, please pick another one</translation>
        </message>
        <message>
            <source>本地模型要写上模型名（例如 qwen2.5:7b，要和你 ollama list 里的一致）</source>
            <translation>A local model needs the model name (for example qwen2.5:7b, matching your ollama list)</translation>
        </message>
    </context>
        <context>
        <name>aipdf::ai::ProviderPreset</name>
        <message>
            <source>演示模式（本地生成，不联网）</source>
            <translation>Demo mode (local generation, offline)</translation>
        </message>
        <message>
            <source>MiniMax</source>
            <translation>MiniMax</translation>
        </message>
        <message>
            <source>DeepSeek</source>
            <translation>DeepSeek</translation>
        </message>
        <message>
            <source>Qwen（阿里云百炼）</source>
            <translation>Qwen (Alibaba Cloud Bailian)</translation>
        </message>
        <message>
            <source>Kimi（月之暗面）</source>
            <translation>Kimi (Moonshot AI)</translation>
        </message>
        <message>
            <source>Ollama（本机模型）</source>
            <translation>Ollama (local model)</translation>
        </message>
        <message>
            <source>自定义 OpenAI 兼容服务</source>
            <translation>Custom OpenAI-compatible service</translation>
        </message>
        <message>
            <source>未知的服务商</source>
            <translation>Unknown provider</translation>
        </message>
    </context>
<context>
        <name>aipdf::app::AccessibilityWizard</name>
        <message>
            <source> 磅</source>
            <translation> pt</translation>
        </message>
        <message>
            <source>%1（%2）</source>
            <translation>%1 (%2)</translation>
        </message>
        <message>
            <source>三步把阅读配置调好：先看得清，再听得清，最后确认。随时可以按「完成」结束。</source>
            <translation>Three steps to set up reading: make it easy to see, easy to hear, then confirm. You can press Done at any time.</translation>
        </message>
        <message>
            <source>上一步</source>
            <translation>Back</translation>
        </message>
        <message>
            <source>下一步</source>
            <translation>Next</translation>
        </message>
        <message>
            <source>完成</source>
            <translation>Done</translation>
        </message>
        <message>
            <source>当前语速: %1</source>
            <translation>Current rate: %1</translation>
        </message>
        <message>
            <source>当前音量: %1</source>
            <translation>Current volume: %1</translation>
        </message>
        <message>
            <source>导航面板: %1</source>
            <translation>Navigation panel: %1</translation>
        </message>
        <message>
            <source>无障碍配置向导</source>
            <translation>Accessibility setup wizard</translation>
        </message>
        <message>
            <source>显示</source>
            <translation>Shown</translation>
        </message>
        <message>
            <source>显示左侧导航面板</source>
            <translation>Show the navigation panel on the left</translation>
        </message>
        <message>
            <source>界面字号:</source>
            <translation>Interface font size:</translation>
        </message>
        <message>
            <source>界面字号: %1 磅</source>
            <translation>Interface font size: %1 pt</translation>
        </message>
        <message>
            <source>导航面板</source>
            <translation>Navigation panel</translation>
        </message>
        <message>
            <source>音色:</source>
            <translation>Voice:</translation>
        </message>
        <message>
            <source>音色: %1</source>
            <translation>Voice: %1</translation>
        </message>
        <message>
            <source>浅色</source>
            <translation>Light</translation>
        </message>
        <message>
            <source>夜间</source>
            <translation>Night</translation>
        </message>
        <message>
            <source>护眼</source>
            <translation>Eye care</translation>
        </message>
        <message>
            <source>高对比</source>
            <translation>High contrast</translation>
        </message>
        <message>
            <source>自动（按文本语言选择）</source>
            <translation>Automatic (picked by the text language)</translation>
        </message>
        <message>
            <source>语速:</source>
            <translation>Speech rate:</translation>
        </message>
        <message>
            <source>语速: %1</source>
            <translation>Speech rate: %1</translation>
        </message>
        <message>
            <source>音量:</source>
            <translation>Volume:</translation>
        </message>
        <message>
            <source>音量: %1</source>
            <translation>Volume: %1</translation>
        </message>
        <message>
            <source>阅读配色:</source>
            <translation>Reading colours:</translation>
        </message>
        <message>
            <source>阅读配色: %1</source>
            <translation>Reading colours: %1</translation>
        </message>
        <message>
            <source>隐藏</source>
            <translation>Hidden</translation>
        </message>
    </context>
<context>
        <name>aipdf::app::AiPanel</name>
        <message>
            <source>%1 · %2</source>
            <translation>%1 · %2</translation>
        </message>
        <message>
            <source>%1 第 %2 页 · %3</source>
            <translation>%1 Page %2 · %3</translation>
        </message>
        <message>
            <source>&lt;b&gt;提示：&lt;/b&gt;上一次还在生成，先点「停止」或等它结束。</source>
            <translation>&lt;b&gt;Note:&lt;/b&gt; The previous response is still generating. Click "Stop" or wait for it to finish.</translation>
        </message>
        <message>
            <source>&lt;b&gt;提示：&lt;/b&gt;先在页面上选中一段文字，再点「翻译」。</source>
            <translation>&lt;b&gt;Note:&lt;/b&gt; Select some text on the page first, then click "Translate".</translation>
        </message>
        <message>
            <source>&lt;b&gt;提示：&lt;/b&gt;先在页面上选中一段文字，再点「解释」。</source>
            <translation>&lt;b&gt;Note:&lt;/b&gt; Select some text on the page first, then click "Explain".</translation>
        </message>
        <message>
            <source>&lt;b&gt;提示：&lt;/b&gt;向量索引建好了吗？%1。请重建索引后再问。</source>
            <translation>&lt;b&gt;Note:&lt;/b&gt; Is the vector index ready? %1. Rebuild the index and ask again.</translation>
        </message>
        <message>
            <source>&lt;b&gt;提示：&lt;/b&gt;没有检索到相关材料，换个说法再问试试。</source>
            <translation>&lt;b&gt;Note:&lt;/b&gt; No relevant material was found. Try rephrasing your question.</translation>
        </message>
        <message>
            <source>&lt;b&gt;提示：&lt;/b&gt;这一页没有可用的文本层。</source>
            <translation>&lt;b&gt;Note:&lt;/b&gt; This page has no usable text layer.</translation>
        </message>
        <message>
            <source>&lt;b&gt;没有发送：&lt;/b&gt;%1</source>
            <translation>&lt;b&gt;Not sent:&lt;/b&gt; %1</translation>
        </message>
        <message>
            <source>&lt;b&gt;错误：&lt;/b&gt;</source>
            <translation>&lt;b&gt;Error:&lt;/b&gt;</translation>
        </message>
        <message>
            <source>&lt;b&gt;错误：&lt;/b&gt;AI 服务还没连上。</source>
            <translation>&lt;b&gt;Error:&lt;/b&gt; The AI service is not connected yet.</translation>
        </message>
        <message>
            <source>&lt;b&gt;错误：&lt;/b&gt;整篇问答还没接上索引。</source>
            <translation>&lt;b&gt;Error:&lt;/b&gt; Whole-document Q&amp;A is not connected to an index yet.</translation>
        </message>
        <message>
            <source>&lt;div class='scope'&gt;▸ 已更新 AI 服务设置：%1&lt;/div&gt;</source>
            <translation>&lt;div class='scope'&gt;▸ AI service settings updated: %1&lt;/div&gt;</translation>
        </message>
        <message>
            <source>&lt;div class='scope'&gt;▸ 本次发送：%1&lt;/div&gt;</source>
            <translation>&lt;div class='scope'&gt;▸ Sending: %1&lt;/div&gt;</translation>
        </message>
        <message>
            <source>&lt;div class='user'&gt;%1&lt;/div&gt;</source>
            <translation>&lt;div class='user'&gt;%1&lt;/div&gt;</translation>
        </message>
        <message>
            <source>&lt;h3&gt;从当前文档开始&lt;/h3&gt;</source>
            <translation>&lt;h3&gt;Start with this document&lt;/h3&gt;</translation>
        </message>
        <message>
            <source>&lt;i&gt;正在建立索引，稍后自动回答…&lt;/i&gt;</source>
            <translation>&lt;i&gt;Building the index; the answer will follow automatically…&lt;/i&gt;</translation>
        </message>
        <message>
            <source>&lt;i&gt;正在提取第 %1 页的文本…&lt;/i&gt;</source>
            <translation>&lt;i&gt;Extracting text from page %1…&lt;/i&gt;</translation>
        </message>
        <message>
            <source>&lt;i&gt;（已停止）&lt;/i&gt;</source>
            <translation>&lt;i&gt;(Stopped)&lt;/i&gt;</translation>
        </message>
        <message>
            <source>&lt;p class='muted'&gt;每次只会送出当前页、当前选区或检索到的几块，发送前会先告诉你范围。&lt;/p&gt;</source>
            <translation>&lt;p class='muted'&gt;Only the current page, current selection, or retrieved chunks are sent, and the scope is shown before sending.&lt;/p&gt;</translation>
        </message>
        <message>
            <source>&lt;p&gt;切到「整篇文档」范围，我会先检索最相关的几块，再回答并给出&lt;strong&gt;可核对的引用&lt;/strong&gt;。&lt;/p&gt;</source>
            <translation>&lt;p&gt;Switch to the "Whole document" scope and I will first retrieve the most relevant chunks, then answer with &lt;strong&gt;verifiable citations&lt;/strong&gt;.&lt;/p&gt;</translation>
        </message>
        <message>
            <source>&lt;p&gt;我可以帮你总结当前页、解释选中的文字、翻译选中的文字。&lt;/p&gt;</source>
            <translation>&lt;p&gt;I can summarize the current page, explain selected text, and translate selected text.&lt;/p&gt;</translation>
        </message>
        <message>
            <source>AI 服务：%1</source>
            <translation>AI service: %1</translation>
        </message>
        <message>
            <source>AI 服务：正在启动</source>
            <translation>AI service: starting</translation>
        </message>
        <message>
            <source>[%1] 第 %2 页 · 已核实</source>
            <translation>[%1] Page %2 · Verified</translation>
        </message>
        <message>
            <source>[%1] 第 %2 页 · 无法核实（%3）</source>
            <translation>[%1] Page %2 · Unverified (%3)</translation>
        </message>
        <message>
            <source>✦  Folixa AI</source>
            <translation>✦  Folixa AI</translation>
        </message>
        <message>
            <source>保存密钥失败</source>
            <translation>Failed to save the key</translation>
        </message>
        <message>
            <source>保存设置失败</source>
            <translation>Failed to save settings</translation>
        </message>
        <message>
            <source>停止</source>
            <translation>Stop</translation>
        </message>
        <message>
            <source>停止这次生成</source>
            <translation>Stop this generation</translation>
        </message>
        <message>
            <source>删除密钥失败</source>
            <translation>Failed to delete the key</translation>
        </message>
        <message>
            <source>发送</source>
            <translation>Send</translation>
        </message>
        <message>
            <source>只送当前页文本或当前选区</source>
            <translation>Send only the current page text or selection</translation>
        </message>
        <message>
            <source>向 AI 提问</source>
            <translation>Ask AI</translation>
        </message>
        <message>
            <source>就当前页提问…</source>
            <translation>Ask about the current page…</translation>
        </message>
        <message>
            <source>已停止</source>
            <translation>Stopped</translation>
        </message>
        <message>
            <source>已就绪</source>
            <translation>Ready</translation>
        </message>
        <message>
            <source>已核实</source>
            <translation>Verified</translation>
        </message>
        <message>
            <source>引用</source>
            <translation>Citations</translation>
        </message>
        <message>
            <source>引用核验：%1 条中 %2 条已核实</source>
            <translation>Citation check: %2 of %1 citations verified</translation>
        </message>
        <message>
            <source>引用核验：%1 条引用全部无法核实</source>
            <translation>Citation check: none of the %1 citations could be verified</translation>
        </message>
        <message>
            <source>引用核验：回答里没有给出引用</source>
            <translation>Citation check: the answer contains no citations</translation>
        </message>
        <message>
            <source>当前页</source>
            <translation>Current page</translation>
        </message>
        <message>
            <source>总结</source>
            <translation>Summarize</translation>
        </message>
        <message>
            <source>总结当前页</source>
            <translation>Summarize the current page</translation>
        </message>
        <message>
            <source>整篇文档</source>
            <translation>Whole document</translation>
        </message>
        <message>
            <source>整篇文档 · 检索到 %1 块 · %2 字</source>
            <translation>Whole document · %1 chunks retrieved · %2 characters</translation>
        </message>
        <message>
            <source>无法核实：%1</source>
            <translation>Unverified: %1</translation>
        </message>
                <message>
            <source>本地模型 · 文档内容不出本机（回答由本机模型生成）</source>
            <translation>Local model · document content never leaves this machine (the answer comes from the local model)</translation>
        </message>
<message>
            <source>本地模式 · 文档内容不出本机（回答由本地演示模型生成）</source>
            <translation>Local mode · Document content never leaves this device (answers come from the local demo model)</translation>
        </message>
        <message>
            <source>本次发送：%1</source>
            <translation>Sending: %1</translation>
        </message>
        <message>
            <source>本次将问整篇文档：先检索最相关的几块，回答里带可核对的引用。</source>
            <translation>This question covers the whole document: the most relevant chunks are retrieved first, and the answer cites verifiable sources.</translation>
        </message>
        <message>
            <source>本次：输入 %1 tokens · 输出 %2 tokens · 约 %3</source>
            <translation>This request: %1 input tokens · %2 output tokens · approx. %3</translation>
        </message>
        <message>
            <source>检索整篇文档，只送最相关的几块，并给出可核对的引用</source>
            <translation>Search the whole document, send only the most relevant chunks, and cite verifiable sources</translation>
        </message>
        <message>
            <source>正在启动</source>
            <translation>Starting</translation>
        </message>
        <message>
            <source>正在生成…已收到 %1 字</source>
            <translation>Generating… %1 characters received</translation>
        </message>
        <message>
            <source>演示模式（未配置密钥）· 回答由本地拼出，不会发到网络</source>
            <translation>Demo mode (no API key) · Answers are assembled locally and never sent over the network</translation>
        </message>
        <message>
            <source>点一条引用，跳到原文并高亮那一块</source>
            <translation>Click a citation to jump to the source and highlight the passage</translation>
        </message>
        <message>
            <source>第 %1 页 · %2 字%3</source>
            <translation>Page %1 · %2 characters%3</translation>
        </message>
        <message>
            <source>翻译</source>
            <translation>Translate</translation>
        </message>
        <message>
            <source>翻译选中的文字</source>
            <translation>Translate the selected text</translation>
        </message>
        <message>
            <source>范围</source>
            <translation>Scope</translation>
        </message>
        <message>
            <source>解释</source>
            <translation>Explain</translation>
        </message>
        <message>
            <source>解释选中的文字</source>
            <translation>Explain the selected text</translation>
        </message>
        <message>
            <source>设置</source>
            <translation>Settings</translation>
        </message>
        <message>
            <source>设置服务商与 API 密钥</source>
            <translation>Configure the provider and API key</translation>
        </message>
        <message>
            <source>跳到第 %1 页</source>
            <translation>Jump to page %1</translation>
        </message>
        <message>
            <source>这份文档主要讲了什么？</source>
            <translation>What is this document mainly about?</translation>
        </message>
        <message>
            <source>配置文件写不进去，检查 %1 是否可写。</source>
            <translation>Could not write the configuration file. Check that %1 is writable.</translation>
        </message>
        <message>
            <source>阅读、理解并定位当前文档内容</source>
            <translation>Read, understand, and locate content in this document</translation>
        </message>
        <message>
            <source>（已截断）</source>
            <translation> (truncated)</translation>
        </message>
        <message>
            <source>，%1 条无法核实</source>
            <translation>, %1 could not be verified</translation>
        </message>
        <message>
            <source>；有标记指向无效引用</source>
            <translation>; some markers point to invalid citations</translation>
        </message>
            <message>
            <source>&lt;p class='muted'&gt;文档内容只会按照上方显示的范围使用。&lt;/p&gt;</source>
            <translation>&lt;p class='muted'&gt;Document content is used only within the range shown above.&lt;/p&gt;</translation>
        </message>
        <message>
            <source>&lt;p&gt;切换到“整篇文档”后，我会检索相关内容并附上可核对的引用。&lt;/p&gt;</source>
            <translation>&lt;p&gt;After switching to "Entire document", I will retrieve the relevant content and attach citations you can check.&lt;/p&gt;</translation>
        </message>
        <message>
            <source>&lt;p&gt;可以总结页面、解释或翻译选区，也可以直接询问文档内容。&lt;/p&gt;</source>
            <translation>&lt;p&gt;You can summarize pages, explain or translate a selection, or ask about the document content directly.&lt;/p&gt;</translation>
        </message>
        <message>
            <source>回答范围</source>
            <translation>Response scope</translation>
        </message>
        <message>
            <source>总结要点</source>
            <translation>Summary key points</translation>
        </message>
        <message>
            <source>新对话</source>
            <translation>New conversation</translation>
        </message>
        <message>
            <source>清空当前对话历史</source>
            <translation>Clear the current conversation history</translation>
        </message>
        <message>
            <source>翻译选区</source>
            <translation>Translate selection</translation>
        </message>
        <message>
            <source>解释选区</source>
            <translation>Explain selection</translation>
        </message>
        <message>
            <source>询问当前文档…</source>
            <translation>Ask about this document…</translation>
        </message>
        <message>
            <source>请总结第 %1 页的要点</source>
            <translation>Please summarize the key points on page %1</translation>
        </message>
        <message>
            <source>请翻译我选中的内容</source>
            <translation>Please translate the selected text</translation>
        </message>
        <message>
            <source>请解释我选中的内容</source>
            <translation>Please explain the selected text</translation>
        </message>
    <message><source>&lt;b&gt;本地离线问答：&lt;/b&gt;%1</source><translation>&lt;b&gt;Local offline Q&amp;amp;A:&lt;/b&gt; %1</translation></message>
    <message><source>先输入一个问题。</source><translation>Type a question first.</translation></message>
    <message><source>在本地索引里检索并摘出答案（不联网、不调模型）</source><translation>Search the local index and extract the answer (no network, no model)</translation></message>
    <message><source>当前文档还没有索引：先在「索引」面板里建立索引，再问。</source><translation>The current document has no index yet: build it in the Index panel first, then ask.</translation></message>
    <message><source>本地离线问答</source><translation>Local offline Q&amp;A</translation></message>
    <message><source>本地索引里还没有内容：先建立索引，再问。</source><translation>The local index is empty: build the index first, then ask.</translation></message>
    <message><source>本地离线问答还没有接上：当前文档没有可检索的索引。</source><translation>Local offline Q&amp;A is not connected yet: the current document has no index to search.</translation></message>
</context>
    <context>
        <name>aipdf::app::AiSettingsDialog</name>
        <message>
            <source> / 百万 tokens</source>
            <translation> / million tokens</translation>
        </message>
        <message>
            <source> 秒</source>
            <translation> s</translation>
        </message>
        <message>
            <source>AI 服务设置</source>
            <translation>AI Service Settings</translation>
        </message>
        <message>
            <source>API 密钥</source>
            <translation>API key</translation>
        </message>
        <message>
            <source>OpenAI 兼容（云端模型）</source>
            <translation>OpenAI-compatible (cloud model)</translation>
        </message>
                <message>
            <source>任何 OpenAI 兼容服务：地址和模型都由你填。</source>
            <translation>Any OpenAI-compatible service: you fill in the endpoint and the model.</translation>
        </message>
<message>
            <source>保存</source>
            <translation>Save</translation>
        </message>
        <message>
            <source>凭据管理器里已经存了这个服务商的密钥；留空表示不修改。</source>
            <translation>A key for this provider is already stored in Credential Manager; leave blank to keep it.</translation>
        </message>
        <message>
            <source>取消</source>
            <translation>Cancel</translation>
        </message>
        <message>
            <source>只发送你在预览里确认过的页；发送前会显示页码与字数。</source>
            <translation>Only pages you confirm in the preview are sent; page numbers and character counts are shown first.</translation>
        </message>
        <message>
            <source>只发送当前页、选区或检索到的几块；发送前会显示范围。</source>
            <translation>Only the current page, selection, or retrieved chunks are sent; the scope is shown first.</translation>
        </message>
                <message>
            <source>在 DeepSeek 开放平台创建密钥；模型可用 deepseek-chat 与 deepseek-reasoner。</source>
            <translation>Create the key on the DeepSeek platform; models include deepseek-chat and deepseek-reasoner.</translation>
        </message>
        <message>
            <source>国内账号用这个地址；海外账号把地址换成 https://api.minimax.io/v1。</source>
            <translation>This endpoint serves mainland-China accounts; for overseas accounts use https://api.minimax.io/v1 instead.</translation>
        </message>
<message>
            <source>密钥保存在 Windows 凭据管理器里，不写进配置文件。</source>
            <translation>The key is stored in Windows Credential Manager, not in the configuration file.</translation>
        </message>
        <message>
            <source>数据模式</source>
            <translation>Data mode</translation>
        </message>
                <message>
            <source>文档内容一律不出本机：云端服务商会退回演示模型，本机模型（如 Ollama）照常可用。</source>
            <translation>Document content never leaves this machine: cloud providers fall back to demo mode, local models (such as Ollama) keep working.</translation>
        </message>
<message>
            <source>文档内容一律不出本机；即使配了密钥也只用本地演示模型。</source>
            <translation>Document content never leaves this device; even with a key, only the local demo model is used.</translation>
        </message>
                <message>
            <source>月之暗面开放平台创建密钥；模型可用 moonshot-v1-8k、32k、128k 与 kimi-k2。</source>
            <translation>Create the key on the Moonshot platform; models include moonshot-v1-8k, 32k, 128k and kimi-k2.</translation>
        </message>
<message>
            <source>服务商</source>
            <translation>Provider</translation>
        </message>
        <message>
            <source>服务地址</source>
            <translation>Endpoint</translation>
        </message>
        <message>
            <source>本地（文档内容不出本机）</source>
            <translation>Local (document content stays on this device)</translation>
        </message>
                <message>
            <source>本机模型不需要密钥，文档内容不会离开这台机器。</source>
            <translation>Local models need no key, and document content never leaves this machine.</translation>
        </message>
        <message>
            <source>本机模型，不需要密钥，文档内容不出本机；模型名要和 ollama list 里一致。</source>
            <translation>Local model: no key needed, document content stays on this machine; the model name must match the one shown by ollama list.</translation>
        </message>
<message>
            <source>模型</source>
            <translation>Model</translation>
        </message>
        <message>
            <source>清除已保存的密钥</source>
            <translation>Clear saved key</translation>
        </message>
        <message>
            <source>演示模式不会联网，密钥与地址都不需要。</source>
            <translation>Demo mode never goes online; no key or endpoint is needed.</translation>
        </message>
                <message>
            <source>演示模式只在本地生成示例回答，不联网、不发送文档。</source>
            <translation>Demo mode only assembles sample answers locally; it never goes online and sends no document.</translation>
        </message>
<message>
            <source>演示模式（本地生成，不联网）</source>
            <translation>Demo mode (local generation, offline)</translation>
        </message>
        <message>
            <source>片段云端（只送当前页/选区或检索块）</source>
            <translation>Chunk cloud (send only the current page/selection or retrieved chunks)</translation>
        </message>
        <message>
            <source>留空密钥表示不修改已保存的值。</source>
            <translation>Leave the key blank to keep the saved value.</translation>
        </message>
        <message>
            <source>粘贴 API 密钥</source>
            <translation>Paste API key</translation>
        </message>
        <message>
            <source>设置有误</source>
            <translation>Invalid settings</translation>
        </message>
        <message>
            <source>超时</source>
            <translation>Timeout</translation>
        </message>
        <message>
            <source>输入单价</source>
            <translation>Input price</translation>
        </message>
        <message>
            <source>输出单价</source>
            <translation>Output price</translation>
        </message>
        <message>
            <source>还没有存过密钥。演示模式会在没有密钥时自动接管。</source>
            <translation>No key has been saved yet. Demo mode takes over automatically when no key is available.</translation>
        </message>
        <message>
            <source>选定页面（只送勾选的页）</source>
            <translation>Selected pages (send only checked pages)</translation>
        </message>
        <message>
            <source>阿里云百炼的兼容模式，密钥在百炼控制台创建。</source>
            <translation>Alibaba Cloud Bailian compatibility mode; create the key in the Bailian console.</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::AnnotationController</name>
        <message>
            <source>声音: %1</source>
            <translation>Sound: %1</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::AttachmentPanel</name>
        <message>
            <source>删除</source>
            <translation>Delete</translation>
        </message>
        <message>
            <source>名称</source>
            <translation>Name</translation>
        </message>
        <message>
            <source>添加附件</source>
            <translation>Add attachment</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::DocumentOutput</name>
        <message>
            <source>写入失败：%1</source>
            <translation>Write failed: %1</translation>
        </message>
        <message>
            <source>打印机错误</source>
            <translation>Printer error</translation>
        </message>
        <message>
            <source>换页失败</source>
            <translation>Failed to start a new page</translation>
        </message>
        <message>
            <source>无法写入：%1</source>
            <translation>Could not write: %1</translation>
        </message>
        <message>
            <source>无法开始打印：%1</source>
            <translation>Could not start printing: %1</translation>
        </message>
        <message>
            <source>没有可导出的文档</source>
            <translation>No document to export</translation>
        </message>
        <message>
            <source>没有可打印的文档</source>
            <translation>No document to print</translation>
        </message>
        <message>
            <source>没有可用的打印机</source>
            <translation>No printer is available</translation>
        </message>
        <message>
            <source>没有目标路径</source>
            <translation>No destination path</translation>
        </message>
        <message>
            <source>目录不存在：%1</source>
            <translation>Directory does not exist: %1</translation>
        </message>
        <message>
            <source>第 %1 页尺寸异常</source>
            <translation>Page %1 has an invalid size</translation>
        </message>
        <message>
            <source>第 %1 页打不开</source>
            <translation>Could not open page %1</translation>
        </message>
        <message>
            <source>第 %1 页渲染失败</source>
            <translation>Failed to render page %1</translation>
        </message>
        <message>
            <source>设备忙</source>
            <translation>The device is busy</translation>
        </message>
        <message>
            <source>输出副本创建失败，批注可能不可见</source>
            <translation>Could not create the output copy; annotations may not be visible</translation>
        </message>
        <message>
            <source>输出副本创建失败：%1</source>
            <translation>Could not create the output copy: %1</translation>
        </message>
        <message>
            <source>输出副本打不开：%1</source>
            <translation>Could not open the output copy: %1</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::DocumentQaController</name>
        <message>
            <source>向量索引写盘失败</source>
            <translation>Failed to write the vector index to disk</translation>
        </message>
        <message>
            <source>嵌入结果数量与块数不一致</source>
            <translation>Embedding result count does not match the number of chunks</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::EditProposalDialog</name>
        <message>
            <source>AI 想对文档做 %1 处修改</source>
            <translation>AI proposes %1 change(s) to the document</translation>
        </message>
        <message>
            <source>AI 提议的修改</source>
            <translation>Proposed Changes from AI</translation>
        </message>
        <message>
            <source>取消</source>
            <translation>Cancel</translation>
        </message>
        <message>
            <source>执行</source>
            <translation>Apply</translation>
        </message>
        <message>
            <source>点「取消」则什么都不做。</source>
            <translation>Clicking "Cancel" does nothing.</translation>
        </message>
        <message>
            <source>确认后这些修改会立即执行，并进入撤销栈（Ctrl+Z 可撤销）。</source>
            <translation>These changes will be applied immediately and added to the undo stack (Ctrl+Z to undo).</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::IndexController</name>
        <message>
            <source>尚未建立索引</source>
            <translation>No index has been built yet</translation>
        </message>
        <message>
            <source>文档句柄不可用</source>
            <translation>Document handle unavailable</translation>
        </message>
        <message>
            <source>索引中 %1/%2 页</source>
            <translation>Indexing %1/%2 pages</translation>
        </message>
        <message>
            <source>索引失败：%1</source>
            <translation>Indexing failed: %1</translation>
        </message>
        <message>
            <source>索引完成：%1 个块（重建 %2 页）</source>
            <translation>Index complete: %1 blocks (%2 pages rebuilt)</translation>
        </message>
        <message>
            <source>索引已取消</source>
            <translation>Indexing cancelled</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::IndexPanel</name>
        <message>
            <source>取消</source>
            <translation>Cancel</translation>
        </message>
        <message>
            <source>取消索引</source>
            <translation>Cancel indexing</translation>
        </message>
        <message>
            <source>尚未建立索引</source>
            <translation>Index not built yet</translation>
        </message>
        <message>
            <source>尚未打开文档</source>
            <translation>No document open</translation>
        </message>
        <message>
            <source>检索关键词</source>
            <translation>Index search keywords</translation>
        </message>
        <message>
            <source>检索索引…</source>
            <translation>Search the index…</translation>
        </message>
        <message>
            <source>第 %1 页</source>
            <translation>Page %1</translation>
        </message>
        <message>
            <source>第 %1 页 · %2</source>
            <translation>Page %1 · %2</translation>
        </message>
        <message>
            <source>索引检索结果</source>
            <translation>Index search results</translation>
        </message>
        <message>
            <source>重建索引</source>
            <translation>Rebuild index</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::MainWindow</name>
                        <message>
            <source>Excel 工作簿 (*.xlsx)</source>
            <translation>Excel workbook (*.xlsx)</translation>
        </message>
                        <message>
            <source>veraPDF 跑了 3 分钟还没结束，已经停掉。</source>
            <translation>veraPDF did not finish within 3 minutes and was stopped.</translation>
        </message>
        <message>
            <source>临时文件写不出来，认证没法跑。</source>
            <translation>Could not write the temporary file, so the check cannot run.</translation>
        </message>
        <message>
            <source>PDF/A 合规认证（veraPDF）</source>
            <translation>PDF/A certification (veraPDF)</translation>
        </message>
        <message>
            <source>PDF/A 合规认证</source>
            <translation>PDF/A certification</translation>
        </message>
<message>
            <source>保留文字层与图片，表格按识别结果写成表格；精确版式、批注与字体样式会简化。</source>
            <translation>Text layer and images are kept; tables are written as tables when recognised; exact layout, annotations and font styles are simplified.</translation>
        </message>
        <message>
            <source>共 %1 张幻灯片（%2 张图片、%3 个表格）</source>
            <translation>%1 slides (%2 images, %3 tables)</translation>
        </message>
<message>
            <source>共 %1 页、%2 张工作表（认出 %3 个表格）</source>
            <translation>%1 pages, %2 worksheets (%3 tables recognised)</translation>
        </message>
        <message>
            <source>PowerPoint 演示文稿（.pptx）</source>
            <translation>PowerPoint presentation (.pptx)</translation>
        </message>
        <message>
            <source>Excel 工作簿（.xlsx）</source>
            <translation>Excel workbook (.xlsx)</translation>
        </message>
        <message>
            <source>Word 文档（.docx）</source>
            <translation>Word document (.docx)</translation>
        </message>
        <message>
            <source>共 %1 页（%2 张图片、%3 个表格，页与页之间有分页符）</source>
            <translation>%1 pages (%2 images, %3 tables; page breaks between pages)</translation>
        </message>
<message>
            <source>关闭</source>
            <translation>Close</translation>
        </message>
                        <message>
            <source>写标签树失败。</source>
            <translation>Writing the tag tree failed.</translation>
        </message>
<message>
            <source>只包含文字层：图片、批注与精确版式不会带过去；表格按识别结果写成表格，认不出的按文字行导出。</source>
            <translation>Text layer only: images, annotations and exact layout are not carried over; tables are written as tables when recognised, otherwise as text lines.</translation>
        </message>
                <message>
            <source>启动不了 veraPDF：%1</source>
            <translation>Could not start veraPDF: %1</translation>
        </message>
<message>
            <source>导出为 %1：%2</source>
            <translation>Exported as %1: %2</translation>
        </message>
                                <message>
            <source>建不出临时目录，认证没法跑。</source>
            <translation>Could not create a temporary directory, so the check cannot run.</translation>
        </message>
        <message>
            <source>插入一个多媒体注解（可先预览解码与播放；注解本身只写结构）</source>
            <translation>Insert a multimedia annotation (you can preview, decode and play it first; the annotation itself only carries the structure)</translation>
        </message>
<message>
            <source>生成 PDF/UA 文件</source>
            <translation>Generate a PDF/UA file</translation>
        </message>
        <message>
            <source>用系统浏览器引擎渲染网页并打印成 PDF（执行 JavaScript、渲染 CSS）</source>
            <translation>Render the web page with the system browser engine and print it to PDF (executes JavaScript, renders CSS)</translation>
        </message>
<message>
            <source>结构元素类型：%1</source>
            <translation>Structure element types: %1</translation>
        </message>
        <message>
            <source>已写入语义标签树：%1 个结构元素。</source>
            <translation>Semantic tag tree written: %1 structure elements.</translation>
        </message>
        <message>
            <source>自动添加文档标签</source>
            <translation>Automatically tag the document</translation>
        </message>
<message>
            <source>要导出成哪种格式？</source>
            <translation>Which format do you want?</translation>
        </message>
<message>
            <source>设置</source>
            <translation>Settings</translation>
        </message>
        <message>
            <source>设置主题、字体和 AI 服务</source>
            <translation>Theme, font, and AI service settings</translation>
        </message>
        <message>
            <source>主题</source>
            <translation>Theme</translation>
        </message>
        <message>
            <source>浅色</source><translation>Light</translation>
        </message>
        <message>
            <source>字体…</source>
            <translation>Font…</translation>
        </message>
                        <message>
            <source>调用本机的 veraPDF 出合规结论（没装 veraPDF 就做不了）</source>
            <translation>Ask the local veraPDF for a compliance verdict (unavailable without veraPDF)</translation>
        </message>
<message>
            <source>这条差异在新文里，当前打开的是原文，页面跳不过去。</source>
            <translation>This difference is in the new document; the open document is the original, so the page cannot be jumped to.</translation>
        </message>
<message>
            <source>选择界面字体</source>
            <translation>Choose interface font</translation>
        </message>
        <message>
            <source>界面字体已更新</source>
            <translation>Interface font updated</translation>
        </message>
        <message>
            <source>AI 设置…</source>
            <translation>AI Settings…</translation>
        </message>
        <message>
            <source>标记</source>
            <translation>Markup</translation>
        </message>
        <message>
            <source>文本</source>
            <translation>Text</translation>
        </message>
        <message>
            <source>图形</source>
            <translation>Shapes</translation>
        </message>
        <message>
            <source>手写</source>
            <translation>Ink</translation>
        </message>
        <message>
            <source>测量与附件</source>
            <translation>Measure and attachments</translation>
        </message>
        <message>
            <source>历史</source>
            <translation>History</translation>
        </message>
        <message>
            <source>第</source>
            <translation>Page</translation>
        </message>

        <message>
            <source>/ %1 页</source>
            <translation>/ %1</translation>
        </message>
        <message>
            <source>/ 0 页</source>
            <translation>/ 0</translation>
        </message>

        <message>
            <source>共享</source>
            <translation>Share</translation>
        </message>
        <message><source>发送</source><translation>Send</translation></message>
        <message><source>邮件</source><translation>Email</translation></message>
        <message><source>企业</source><translation>Enterprise</translation></message>
        <message><source>共享协作</source><translation>Share link</translation></message>
        <message><source>PDF 协作</source><translation>PDF collaboration</translation></message>
        <message><source>管理共享审阅</source><translation>Manage shared review</translation></message>
        <message><source>文件审阅</source><translation>File review</translation></message>
        <message><source>邮件审阅</source><translation>Email review</translation></message>
        <message><source>请先打开并保存一份 PDF。</source><translation>Open and save a PDF first.</translation></message>
        <message><source>请先打开一份 PDF。</source><translation>Open a PDF first.</translation></message>
        <message><source>保存后共享</source><translation>Save before sharing</translation></message>
        <message><source>当前文档有未保存的修改。要先保存最新版本再共享吗？</source><translation>The document has unsaved changes. Save the latest version before sharing?</translation></message>
        <message><source>当前 PDF 尚未保存到磁盘。</source><translation>The current PDF has not been saved to disk.</translation></message>
        <message><source>请审阅：%1</source><translation>Please review: %1</translation></message>
        <message><source>请审阅：</source><translation>Please review: </translation></message>
        <message><source>请审阅随邮件发送的 PDF：
%1</source><translation>Please review the PDF sent with this email:
%1</translation></message>
        <message><source>请审阅随邮件发送的 PDF：
</source><translation>Please review the PDF sent with this email:
</translation></message>
        <message><source>无法打开系统默认邮件客户端。</source><translation>Could not open the default email client.</translation></message>
        <message><source>已打开邮件客户端；PDF 路径已复制，可粘贴为附件。</source><translation>The email client is open. The PDF path was copied so you can paste it as an attachment.</translation></message>
        <message><source>已复制当前 PDF 的文件链接。</source><translation>Copied the current PDF file link.</translation></message>
        <message>
            <source>输出</source>
            <translation>Output</translation>
        </message>
        <message>
            <source>内容</source>
            <translation>Content</translation>
        </message>
        <message>
            <source>到 Word</source>
            <translation>To Word</translation>
        </message>
        <message>
            <source>到 Excel</source>
            <translation>To Excel</translation>
        </message>
        <message>
            <source>到 HTML</source>
            <translation>To HTML</translation>
        </message>
        <message>
            <source>编辑文本</source>
            <translation>Edit text</translation>
        </message>
        <message>
            <source>编辑对象</source>
            <translation>Edit objects</translation>
        </message>
        <message>
            <source>查找替换</source>
            <translation>Find and replace</translation>
        </message>
        <message>
            <source>填写表单</source>
            <translation>Fill forms</translation>
        </message>
        <message>
            <source>表单数据</source>
            <translation>Form data</translation>
        </message>
        <message>
            <source>表单模板</source>
            <translation>Form templates</translation>
        </message>
        <message>
            <source>协作</source>
            <translation>Collaboration</translation>
        </message>
        <message>
            <source>邮件发送</source>
            <translation>Send by email</translation>
        </message>
        <message>
            <source>共享审阅</source>
            <translation>Shared review</translation>
        </message>
        <message>
            <source>追踪器</source>
            <translation>Tracker</translation>
        </message>

        <message>
            <source>插入文件</source>
            <translation>Insert file</translation>
        </message>
        <message>
            <source>提取范围</source>
            <translation>Extract range</translation>
        </message>

        <message>
            <source>替换</source>
            <translation>Replace</translation>
        </message>
        <message>
            <source>交叉合并</source>
            <translation>Cross-merge</translation>
        </message>
        <message>
            <source>调整大小</source>
            <translation>Resize</translation>
        </message>
        <message>
            <source>背景</source>
            <translation>Background</translation>
        </message>
        <message>
            <source>复制页</source>
            <translation>Duplicate page</translation>
        </message>
        <message>
            <source>交换</source>
            <translation>Swap</translation>
        </message>
        <message>
            <source>逆序</source>
            <translation>Reverse</translation>
        </message>
        <message>
            <source>拆分</source>
            <translation>Split</translation>
        </message>
        <message>
            <source>主页</source>
            <translation>Home</translation>
        </message>
        <message>
            <source>审阅</source>
            <translation>Review</translation>
        </message>
        <message>
            <source>导出</source>
            <translation>Export</translation>
        </message>
        <message>
            <source>组装</source>
            <translation>Assemble</translation>
        </message>
        <message>
            <source>装饰</source>
            <translation>Decorate</translation>
        </message>
                <message>
            <source>需人工确认：%1</source>
            <translation>Needs manual confirmation: %1</translation>
        </message>
<message>
            <source>面板</source>
            <translation>Panels</translation>
        </message>
        <message>
            <source>页面管理</source>
            <translation>Page management</translation>
        </message>
        <message>
            <source>首页</source>
            <translation>Home</translation>
        </message>
        <message>
            <source>编辑</source>
            <translation>Edit</translation>
        </message>
        <message>
            <source>对话</source>
            <translation>Chat</translation>
        </message>
        <message>
            <source>视图</source>
            <translation>View</translation>
        </message>
        <message>
            <source>文件</source>
            <translation>File</translation>
        </message>
        <message>
            <source>文档安全</source>
            <translation>Document security</translation>
        </message>
        <message>
            <source>文档操作</source>
            <translation>Document actions</translation>
        </message>
        <message>
            <source>显示</source>
            <translation>Display</translation>
        </message>
        <message>
            <source>注释</source>
            <translation>Annotations</translation>
        </message>
        <message>
            <source>安装</source>
            <translation>Install</translation>
        </message>
        <message>
            <source>装载</source>
            <translation>Load</translation>
        </message>
        <message>
            <source>视图</source>
            <translation>View</translation>
        </message>
        <message>
            <source>检查</source>
            <translation>Check</translation>
        </message>
        <message>
            <source>页面布局</source>
            <translation>Page layout</translation>
        </message>
        <message>
            <source>页面显示</source>
            <translation>Page display</translation>
        </message>
        <message>
            <source>加密与权限…</source>
            <translation>Encryption and permissions…</translation>
        </message>
        <message>
            <source>导出 FDF…</source>
            <translation>Export FDF…</translation>
        </message>
        <message>
            <source>导入 FDF…</source>
            <translation>Import FDF…</translation>
        </message>
        <message>
            <source>录制声音批注…</source>
            <translation>Record sound annotation…</translation>
        </message>
        <message>
            <source>AI 助手</source>
            <translation>AI Assistant</translation>
        </message>
        <message>
            <source>AI 快捷操作</source>
            <translation>AI quick actions</translation>
        </message>
        <message>
            <source>AI：找出空白页并提议删除</source>
            <translation>AI: find blank pages and propose deletion</translation>
        </message>
        <message>
            <source>CSV (*.csv)</source>
            <translation>CSV (*.csv)</translation>
        </message>
        <message>
            <source>Folixa 文档工作区</source>
            <translation>Folixa document workspace</translation>
        </message>
        <message>
            <source>PDF 文件 (*.pdf)</source>
            <translation>PDF files (*.pdf)</translation>
        </message>
        <message>
            <source>PDF 文档 (*.pdf)</source>
            <translation>PDF documents (*.pdf)</translation>
        </message>
        <message>
            <source>✦ AI 助手</source>
            <translation>✦ AI Assistant</translation>
        </message>
        <message>
            <source>「%1」没有可跳转的目标</source>
            <translation>"%1" has no jump target</translation>
        </message>
        <message>
            <source>「解释」会用到你选中的这段文字</source>
            <translation>"Explain" uses the text you selected</translation>
        </message>
        <message>
            <source>上次在 %1 的修改没有保存（%2 条操作）。要恢复吗？</source>
            <translation>Changes to %1 were not saved (%2 operations). Restore them?</translation>
        </message>
        <message>
            <source>上移</source>
            <translation>Move up</translation>
        </message>
        <message>
            <source>下拉框</source>
            <translation>Drop-down</translation>
        </message>
        <message>
            <source>下移</source>
            <translation>Move down</translation>
        </message>
        <message>
            <source>专注模式</source>
            <translation>Focus mode</translation>
        </message>
        <message>
            <source>丢弃</source>
            <translation>Discard</translation>
        </message>
        <message>
            <source>主菜单</source>
            <translation>Main menu</translation>
        </message>
        <message>
            <source>书签</source>
            <translation>Bookmarks</translation>
        </message>
        <message>
            <source>保存</source>
            <translation>Save</translation>
        </message>
        <message>
            <source>保存失败：%1</source>
            <translation>Save failed: %1</translation>
        </message>
        <message>
            <source>保护</source>
            <translation>Protect</translation>
        </message>
        <message>
            <source>值</source>
            <translation>Value</translation>
        </message>
        <message>
            <source>先在页面上选中一段文字</source>
            <translation>Select some text on the page first</translation>
        </message>
        <message>
            <source>先打开一份 PDF</source>
            <translation>Open a PDF first</translation>
        </message>
        <message>
            <source>先选中一个字段</source>
            <translation>Select a field first</translation>
        </message>
        <message>
            <source>先选中要改的批注</source>
            <translation>Select the annotations to change first</translation>
        </message>
        <message>
            <source>写不了这个文件</source>
            <translation>Cannot write this file</translation>
        </message>
        <message>
            <source>写入失败：这个字段可能只读</source>
            <translation>Write failed: the field may be read-only</translation>
        </message>
        <message>
            <source>写入所选字段</source>
            <translation>Write to the selected field</translation>
        </message>
        <message>
            <source>列表框</source>
            <translation>List box</translation>
        </message>
        <message>
            <source>删除</source>
            <translation>Delete</translation>
        </message>
        <message>
            <source>删除当前页</source>
            <translation>Delete the current page</translation>
        </message>
        <message>
            <source>删除所选批注</source>
            <translation>Delete selected annotations</translation>
        </message>
        <message>
            <source>功能导航</source>
            <translation>Feature navigation</translation>
        </message>
        <message>
            <source>单选</source>
            <translation>Radio button</translation>
        </message>
        <message>
            <source>单页</source>
            <translation>Single page</translation>
        </message>
        <message>
            <source>双页</source>
            <translation>Two pages</translation>
        </message>
        <message>
            <source>双页（封面）</source>
            <translation>Two pages (separate cover)</translation>
        </message>
        <message>
            <source>发现未保存的工作</source>
            <translation>Unsaved work found</translation>
        </message>
        <message>
            <source>另存为</source>
            <translation>Save as</translation>
        </message>
        <message>
            <source>另存为…</source>
            <translation>Save as…</translation>
        </message>
        <message>
            <source>右转</source>
            <translation>Rotate right</translation>
        </message>
        <message>
            <source>名称</source>
            <translation>Name</translation>
        </message>
        <message>
            <source>回复内容:</source>
            <translation>Reply:</translation>
        </message>
        <message>
            <source>回复所选</source>
            <translation>Reply to selected</translation>
        </message>
        <message>
            <source>回复批注</source>
            <translation>Reply to annotation</translation>
        </message>
        <message>
            <source>回复没有添加成功</source>
            <translation>The reply was not added</translation>
        </message>
        <message>
            <source>在当前页后插入一张空白页</source>
            <translation>Insert a blank page after the current page</translation>
        </message>
        <message>
            <source>复选框</source>
            <translation>Check box</translation>
        </message>
        <message>
            <source>审阅状态</source>
            <translation>Review status</translation>
        </message>
        <message>
            <source>导入 CSV…</source>
            <translation>Import CSV…</translation>
        </message>
        <message>
            <source>导入了 %1 个字段</source>
            <translation>Imported %1 fields</translation>
        </message>
        <message>
            <source>导入批注（XFDF）…</source>
            <translation>Import annotations (XFDF)…</translation>
        </message>
        <message>
            <source>导入表单数据</source>
            <translation>Import form data</translation>
        </message>
        <message>
            <source>导出 CSV…</source>
            <translation>Export CSV…</translation>
        </message>
        <message>
            <source>导出批注摘要（HTML）…</source>
            <translation>Export annotation summary (HTML)…</translation>
        </message>
        <message>
            <source>导出批注（XFDF）…</source>
            <translation>Export annotations (XFDF)…</translation>
        </message>
        <message>
            <source>导出文本…</source>
            <translation>Export text…</translation>
        </message>
        <message>
            <source>导出表单数据</source>
            <translation>Export form data</translation>
        </message>
        <message>
            <source>导出页面为图片…</source>
            <translation>Export pages as images…</translation>
        </message>
        <message>
            <source>导航</source>
            <translation>Navigation</translation>
        </message>
        <message>
            <source>将在后续版本中提供</source>
            <translation>Coming in a later version</translation>
        </message>
        <message>
            <source>就绪</source>
            <translation>Ready</translation>
        </message>
        <message>
            <source>工具</source>
            <translation>Tools</translation>
        </message>
        <message>
            <source>左转</source>
            <translation>Rotate left</translation>
        </message>
        <message>
            <source>已保存 %1</source>
            <translation>Saved %1</translation>
        </message>
        <message>
            <source>已保存 %1（原文件备份为 %2）</source>
            <translation>Saved %1 (original backed up as %2)</translation>
        </message>
        <message>
            <source>已写入 %1</source>
            <translation>Wrote %1</translation>
        </message>
        <message>
            <source>已删除 %1 个空白页，可用 Ctrl+Z 撤销</source>
            <translation>Deleted %1 blank pages; press Ctrl+Z to undo</translation>
        </message>
        <message>
            <source>已删除批注</source>
            <translation>Annotation deleted</translation>
        </message>
        <message>
            <source>已取消打开 %1</source>
            <translation>Cancelled opening %1</translation>
        </message>
        <message>
            <source>已取消，文档没有任何改动</source>
            <translation>Cancelled; the document was not changed</translation>
        </message>
        <message>
            <source>已另存为 %1</source>
            <translation>Saved as %1</translation>
        </message>
        <message>
            <source>已复制 %1 个字符到剪贴板</source>
            <translation>Copied %1 characters to the clipboard</translation>
        </message>
        <message>
            <source>已导出表单数据</source>
            <translation>Exported the form data</translation>
        </message>
        <message>
            <source>已恢复 %1 条未保存的操作</source>
            <translation>Restored %1 unsaved operations</translation>
        </message>
        <message>
            <source>已扁平化第 %1 页的表单</source>
            <translation>Flattened the form on page %1</translation>
        </message>
        <message>
            <source>已打开 %1（%2 页）</source>
            <translation>Opened %1 (%2 pages)</translation>
        </message>
        <message>
            <source>已打开外部链接：%1</source>
            <translation>Opened external link: %1</translation>
        </message>
        <message>
            <source>已更新 %1 条批注的外观</source>
            <translation>Updated the appearance of %1 annotations</translation>
        </message>
        <message>
            <source>已添加回复</source>
            <translation>Reply added</translation>
        </message>
        <message>
            <source>已跳转到第 %1 页</source>
            <translation>Jumped to page %1</translation>
        </message>
        <message>
            <source>带 AcroForm 的 PDF 会在这里列出可填写的字段。</source>
            <translation>PDFs with an AcroForm list their fillable fields here.</translation>
        </message>
        <message>
            <source>应用到所选批注</source>
            <translation>Apply to selected annotations</translation>
        </message>
        <message>
            <source>建立文档索引并按关键词检索</source>
            <translation>Build a document index and search by keyword</translation>
        </message>
        <message>
            <source>开始聊天</source>
            <translation>Start chatting</translation>
        </message>
        <message>
            <source>当前文档没有可显示的书签。</source>
            <translation>This document has no bookmarks to show.</translation>
        </message>
        <message>
            <source>当前文档没有大纲（书签）。</source>
            <translation>This document has no outline (bookmarks).</translation>
        </message>
        <message>
            <source>总结</source>
            <translation>Summarize</translation>
        </message>
        <message>
            <source>总结当前页</source>
            <translation>Summarize the current page</translation>
        </message>
        <message>
            <source>恢复</source>
            <translation>Restore</translation>
        </message>
        <message>
            <source>扁平化失败</source>
            <translation>Flattening failed</translation>
        </message>
        <message>
            <source>扁平化本页</source>
            <translation>Flatten this page</translation>
        </message>
        <message>
            <source>打印…</source>
            <translation>Print…</translation>
        </message>
        <message>
            <source>打印批注摘要…</source>
            <translation>Print annotation summary…</translation>
        </message>
        <message>
            <source>打开 PDF</source>
            <translation>Open PDF</translation>
        </message>
        <message>
            <source>打开 PDF…</source>
            <translation>Open PDF…</translation>
        </message>
        <message>
            <source>打开一份 PDF 后，这些动作才有可用的内容</source>
            <translation>Open a PDF to give these actions something to work on</translation>
        </message>
        <message>
            <source>打开完整对话面板，自己提问</source>
            <translation>Open the full chat panel and ask your own question</translation>
        </message>
        <message>
            <source>执行失败：%1</source>
            <translation>Failed: %1</translation>
        </message>
        <message>
            <source>扫描了 %1 页，没有发现空白页</source>
            <translation>Scanned %1 pages; no blank pages found</translation>
        </message>
        <message>
            <source>批注</source>
            <translation>Annotations</translation>
        </message>
        <message>
            <source>批注颜色</source>
            <translation>Annotation color</translation>
        </message>
        <message>
            <source>把当前页往前挪一位</source>
            <translation>Move the current page one position earlier</translation>
        </message>
        <message>
            <source>把当前页往后挪一位</source>
            <translation>Move the current page one position later</translation>
        </message>
        <message>
            <source>把当前页逆时针转 90 度</source>
            <translation>Rotate the current page 90° counterclockwise</translation>
        </message>
        <message>
            <source>把当前页顺时针转 90 度</source>
            <translation>Rotate the current page 90° clockwise</translation>
        </message>
        <message>
            <source>按钮</source>
            <translation>Button</translation>
        </message>
        <message>
            <source>提案没有通过校验：%1</source>
            <translation>The proposal failed validation: %1</translation>
        </message>
        <message>
            <source>插入</source>
            <translation>Insert</translation>
        </message>
        <message>
            <source>搜索</source>
            <translation>Search</translation>
        </message>
        <message>
            <source>改写</source>
            <translation>Rewrite</translation>
        </message>
        <message>
            <source>放映</source>
            <translation>Presentation</translation>
        </message>
        <message>
            <source>整理页面：插入、删除、旋转与移动</source>
            <translation>Organize pages: insert, delete, rotate, move</translation>
        </message>
        <message>
            <source>文件(&amp;F)</source>
            <translation>File(&amp;F)</translation>
        </message>
        <message>
            <source>文本框</source>
            <translation>Text box</translation>
        </message>
        <message>
            <source>无法打开 %1：%2</source>
            <translation>Cannot open %1: %2</translation>
        </message>
        <message>
            <source>无法执行：%1</source>
            <translation>Cannot run: %1</translation>
        </message>
        <message>
            <source>显示导航面板</source>
            <translation>Show navigation panel</translation>
        </message>
        <message>
            <source>暂无书签</source>
            <translation>No bookmarks</translation>
        </message>
        <message>
            <source>暂无批注</source>
            <translation>No annotations</translation>
        </message>
        <message>
            <source>暂无文档目录</source>
            <translation>No document outline</translation>
        </message>
        <message>
            <source>未知</source>
            <translation>Unknown</translation>
        </message>
        <message>
            <source>松开即可打开 PDF</source>
            <translation>Release to open the PDF</translation>
        </message>
        <message>
            <source>查找…</source>
            <translation>Find…</translation>
        </message>
        <message>
            <source>查看与删除批注</source>
            <translation>View and delete annotations</translation>
        </message>
        <message>
            <source>校对</source>
            <translation>Proofread</translation>
        </message>
        <message>
            <source>欢迎页</source>
            <translation>Welcome</translation>
        </message>
        <message>
            <source>工作台</source>
            <translation>Workspace</translation>
        </message>
        <message>
            <source>工作台：最近文档</source>
            <translation>Workspace: recent documents</translation>
        </message>
        <message>
            <source>正在识别第 %1 页…</source>
            <translation>Recognizing page %1…</translation>
        </message>
        <message>
            <source>清除状态</source>
            <translation>Clear status</translation>
        </message>
        <message>
            <source>状态:</source>
            <translation>Status:</translation>
        </message>
        <message>
            <source>界面语言已切换为 %1</source>
            <translation>Interface language set to %1</translation>
        </message>
        <message>
            <source>目录</source>
            <translation>Outline</translation>
        </message>
        <message>
            <source>第 %1 页 OCR 失败：%2</source>
            <translation>OCR failed on page %1: %2</translation>
        </message>
        <message>
            <source>第 %1 页 OCR 完成：%2 块已加入索引</source>
            <translation>OCR finished on page %1: %2 blocks added to the index</translation>
        </message>
        <message>
            <source>第 %1 页 · %2</source>
            <translation>Page %1 · %2</translation>
        </message>
        <message>
            <source>签名</source>
            <translation>Signature</translation>
        </message>
        <message>
            <source>类型</source>
            <translation>Type</translation>
        </message>
        <message>
            <source>索引</source>
            <translation>Index</translation>
        </message>
        <message>
            <source>线宽</source>
            <translation>Width</translation>
        </message>
        <message>
            <source>缓存 0 MiB · 待渲染 0</source>
            <translation>Cache 0 MiB · 0 pending</translation>
        </message>
        <message>
            <source>编辑</source>
            <translation>Edit</translation>
        </message>
        <message>
            <source>表单</source>
            <translation>Forms</translation>
        </message>
        <message>
            <source>视图(&amp;V)</source>
            <translation>View(&amp;V)</translation>
        </message>
        <message>
            <source>解释</source>
            <translation>Explain</translation>
        </message>
        <message>
            <source>解释页面上选中的文字</source>
            <translation>Explain the selected text on the page</translation>
        </message>
        <message>
            <source>设置审阅状态</source>
            <translation>Set review status</translation>
        </message>
        <message>
            <source>识别当前页（OCR）</source>
            <translation>Recognize current page (OCR)</translation>
        </message>
        <message>
            <source>该条目没有可跳转的目标</source>
            <translation>This entry has no jump target</translation>
        </message>
        <message>
            <source>语言(&amp;L)</source>
            <translation>Language(&amp;L)</translation>
        </message>
        <message>
            <source>请输入 PDF 密码：</source>
            <translation>Enter the PDF password:</translation>
        </message>
        <message>
            <source>读不了这个文件</source>
            <translation>Cannot read this file</translation>
        </message>
        <message>
            <source>跟随系统（%1）</source>
            <translation>Follow system (%1)</translation>
        </message>
        <message>
            <source>跳转到第 %1 页</source>
            <translation>Jump to page %1</translation>
        </message>
        <message>
            <source>转换</source>
            <translation>Convert</translation>
        </message>
        <message>
            <source>输入要写入的值</source>
            <translation>Enter the value to write</translation>
        </message>
        <message>
            <source>这份文档没有表单字段</source>
            <translation>This document has no form fields</translation>
        </message>
        <message>
            <source>退出</source>
            <translation>Exit</translation>
        </message>
        <message>
            <source>选中文字后可「解释」；「总结」作用于当前页</source>
            <translation>Select text to use "Explain"; "Summarize" works on the current page</translation>
        </message>
        <message>
            <source>选中文字后点「高亮」，或用便签、矩形、墨迹工具添加批注。</source>
            <translation>Select text and click "Highlight", or add notes, rectangles, and ink with the drawing tools.</translation>
        </message>
        <message>
            <source>透明</source>
            <translation>Opacity</translation>
        </message>
        <message>
            <source>阅读</source>
            <translation>Read</translation>
        </message>
        <message>
            <source>阅读视图</source>
            <translation>Reading view</translation>
        </message>
        <message>
            <source>需要密码</source>
            <translation>Password required</translation>
        </message>
        <message>
            <source>页</source>
            <translation>Page</translation>
        </message>
        <message>
            <source>页面</source>
            <translation>Pages</translation>
        </message>
        <message>
            <source>页面缓存占用与待完成渲染任务数</source>
            <translation>Page cache usage and pending render jobs</translation>
        </message>
        <message>
            <source>颜色…</source>
            <translation>Color…</translation>
        </message>
        <message>
            <source>默认</source>
            <translation>Default</translation>
        </message>
        <message>
            <source>电子签章</source>
            <translation>E-Sign</translation>
        </message>
        <message>
            <source>辅助工具</source>
            <translation>Accessibility</translation>
        </message>
        <message>
            <source>论文工具</source>
            <translation>Thesis Tools</translation>
        </message>
        <message>
            <source>文档管理</source>
            <translation>Document Management</translation>
        </message>
        <message>
            <source>组织</source>
            <translation>Organize</translation>
        </message>
        <message>
            <source>检索</source>
            <translation>Search</translation>
        </message>
        <message>
            <source>发票</source>
            <translation>Invoices</translation>
        </message>
        <message>
            <source>批量</source>
            <translation>Batch</translation>
        </message>
        <message>
            <source>打开本地全文索引与检索</source>
            <translation>Open local full-text indexing and search</translation>
        </message>
        <message><source>按统一前缀和序号重命名多个文件</source><translation>Rename multiple files using a common prefix and sequence number</translation></message>
        <message><source>选择要批量重命名的文件</source><translation>Select files to batch rename</translation></message>
        <message><source>文档 (*.pdf *.ofd);;PDF 文件 (*.pdf);;所有文件 (*)</source><translation>Documents (*.pdf *.ofd);;PDF files (*.pdf);;All files (*)</translation></message>
        <message><source>批量重命名</source><translation>Batch Rename</translation></message>
        <message><source>文件名前缀:</source><translation>File name prefix:</translation></message>
        <message><source>文档</source><translation>Document</translation></message>
        <message><source>批量重命名失败</source><translation>Batch Rename Failed</translation></message>
        <message><source>已重命名 %1 个文件</source><translation>Renamed %1 files</translation></message>
        <message>
            <source>录制</source>
            <translation>Record</translation>
        </message>
        <message>
            <source>帮助</source>
            <translation>Help</translation>
        </message>
        <message><source>产品</source><translation>Product</translation></message>
        <message><source>无法打开链接</source><translation>Could Not Open Link</translation></message>
        <message><source>无法调用系统浏览器，请手动访问：
%1</source><translation>Could not launch the system browser. Open this address manually:
%1</translation></message>
        <message><source>命令行帮助</source><translation>Command-Line Help</translation></message>
        <message><source>用法：
  AiPdfEditor.exe [PDF 文件]

示例：
  AiPdfEditor.exe C:\Documents\example.pdf

把 PDF 路径作为第一个参数传入，即可在启动时直接打开文档。</source><translation>Usage:
  AiPdfEditor.exe [PDF file]

Example:
  AiPdfEditor.exe C:\Documents\example.pdf

Pass a PDF path as the first argument to open the document at startup.</translation></message>
        <message><source>无法打开设置</source><translation>Could Not Open Settings</translation></message>
        <message><source>请在 Windows 设置中打开“应用 &gt; 默认应用”，然后为 .pdf 选择 Folixa。</source><translation>Open Apps &gt; Default apps in Windows Settings, then select Folixa for .pdf files.</translation></message>
        <message><source>默认 PDF 阅读器</source><translation>Default PDF Reader</translation></message>
        <message><source>请在系统设置中把 Folixa 设为 .pdf 文件的默认应用。</source><translation>Set Folixa as the default application for .pdf files in system settings.</translation></message>
        <message><source>关于 Folixa</source><translation>About Folixa</translation></message>
        <message><source>&lt;h3&gt;Folixa · AI PDF Editor %1&lt;/h3&gt;&lt;p&gt;原生、轻量的 PDF 阅读与编辑工具。&lt;/p&gt;&lt;p&gt;基于 Qt 6 与 PDFium 构建。&lt;/p&gt;&lt;p&gt;项目主页：&lt;a href="https://github.com/fengjutian/Folixa"&gt;github.com/fengjutian/Folixa&lt;/a&gt;&lt;/p&gt;</source><translation>&lt;h3&gt;Folixa · AI PDF Editor %1&lt;/h3&gt;&lt;p&gt;A native, lightweight PDF reader and editor.&lt;/p&gt;&lt;p&gt;Built with Qt 6 and PDFium.&lt;/p&gt;&lt;p&gt;Project: &lt;a href="https://github.com/fengjutian/Folixa"&gt;github.com/fengjutian/Folixa&lt;/a&gt;&lt;/p&gt;</translation></message>
        <message><source>Folixa 用户手册</source><translation>Folixa User Guide</translation></message>
        <message><source>&lt;h3&gt;快速开始&lt;/h3&gt;&lt;p&gt;&lt;b&gt;打开文档&lt;/b&gt;：使用“文件 &amp;gt; 打开”，把 PDF 拖入窗口，或在命令行中传入文件路径。&lt;/p&gt;&lt;p&gt;&lt;b&gt;阅读&lt;/b&gt;：在“主页”调整缩放和旋转；在“视图”切换连续、单页或双页布局。&lt;/p&gt;&lt;p&gt;&lt;b&gt;编辑&lt;/b&gt;：使用“编辑”“注释”“页面管理”和“表单”中的工具。修改后按 Ctrl+S 保存，或使用“另存为”保留原文件。&lt;/p&gt;&lt;p&gt;&lt;b&gt;搜索与 AI&lt;/b&gt;：左侧搜索面板用于全文查找；AI 助手只会在你主动操作时发送所选文档内容。&lt;/p&gt;&lt;p&gt;&lt;b&gt;获取帮助&lt;/b&gt;：遇到问题可打开“帮助中心”或“发表想法”。&lt;/p&gt;</source><translation>&lt;h3&gt;Quick Start&lt;/h3&gt;&lt;p&gt;&lt;b&gt;Open a document&lt;/b&gt;: choose File &amp;gt; Open, drag a PDF into the window, or pass its path on the command line.&lt;/p&gt;&lt;p&gt;&lt;b&gt;Read&lt;/b&gt;: adjust zoom and rotation on Home; switch between continuous, single-page, and facing layouts on View.&lt;/p&gt;&lt;p&gt;&lt;b&gt;Edit&lt;/b&gt;: use the tools on Edit, Comment, Page Management, and Form. Press Ctrl+S to save, or use Save As to preserve the original.&lt;/p&gt;&lt;p&gt;&lt;b&gt;Search and AI&lt;/b&gt;: use the left search panel for full-text search. The AI assistant sends selected document content only when you explicitly request it.&lt;/p&gt;&lt;p&gt;&lt;b&gt;Get help&lt;/b&gt;: open Help Center or Share Feedback when you encounter a problem.&lt;/p&gt;</translation></message>
        <message><source>产品信息</source><translation>Product Information</translation></message>
        <message><source>Folixa %1

本地优先的原生 PDF 阅读与编辑器。
支持阅读、批注、页面管理、表单、OCR、全文检索与 AI 辅助。

Folixa 不需要购买或激活，也不提供付费订阅。</source><translation>Folixa %1

A local-first, native PDF reader and editor.
Supports reading, comments, page management, forms, OCR, full-text search, and AI assistance.

Folixa requires no purchase or activation and offers no paid subscription.</translation></message>
        <message><source>屏幕录制</source><translation>Screen Recording</translation></message>
        <message><source>无法打开 Windows 游戏栏。请在“设置 &gt; 游戏 &gt; Xbox Game Bar”中启用它，或按 Win+Alt+R 开始录制。</source><translation>Could not open Windows Game Bar. Enable it under Settings &gt; Gaming &gt; Xbox Game Bar, or press Win+Alt+R to start recording.</translation></message>
        <message><source>已打开 Windows 游戏栏；按 Win+Alt+R 开始或停止录制。</source><translation>Windows Game Bar is open. Press Win+Alt+R to start or stop recording.</translation></message>
        <message><source>请使用系统自带的屏幕录制工具录制 Folixa 窗口。</source><translation>Use your system screen recorder to record the Folixa window.</translation></message>
        <message><source>请先打开一份 PDF。</source><translation>Open a PDF first.</translation></message>
        <message><source>保存截图</source><translation>Save Screenshot</translation></message>
        <message><source>PNG 图片 (*.png)</source><translation>PNG Image (*.png)</translation></message>
        <message><source>截图无法写入所选位置。</source><translation>The screenshot could not be written to the selected location.</translation></message>
        <message><source>截图已保存到 %1</source><translation>Screenshot saved to %1</translation></message>
        <message><source>声音录制保存在声音批注中。</source><translation>Audio recordings are stored as sound annotations.</translation></message>
        <message><source>转为 PDF</source><translation>Convert to PDF</translation></message>
        <message><source>归档与优化</source><translation>Archive and optimize</translation></message>
        <message><source>更多转换</source><translation>More conversions</translation></message>
        <message><source>Office 文档 (*.doc *.docx *.xls *.xlsx *.ppt *.pptx *.odt *.ods *.odp)</source><translation>Office documents (*.doc *.docx *.xls *.xlsx *.ppt *.pptx *.odt *.ods *.odp)</translation></message>
        <message><source>HTML 文档 (*.html *.htm)</source><translation>HTML documents (*.html *.htm)</translation></message>
        <message><source>选择要转换的文件</source><translation>Select a file to convert</translation></message>
        <message><source>保存转换结果</source><translation>Save conversion result</translation></message>
        <message><source>输出文件不能覆盖输入文件，请选择其他名称。</source><translation>The output cannot overwrite the input file. Choose another name.</translation></message>
        <message><source>无法创建安全的临时目录。</source><translation>Could not create a secure temporary directory.</translation></message>
        <message><source>缺少转换引擎</source><translation>Conversion engine missing</translation></message>
        <message><source>%1

Office/HTML 转换需要 LibreOffice；PDF/A 和优化压缩需要 Ghostscript。安装后重新执行即可，Folixa 不会上传文档。</source><translation>%1

Office/HTML conversion requires LibreOffice; PDF/A and optimization require Ghostscript. Install the required engine and try again. Folixa does not upload the document.</translation></message>
        <message><source>文档转换</source><translation>Document conversion</translation></message>
        <message><source>正在使用 %1 转换…</source><translation>Converting with %1…</translation></message>
        <message><source>转换失败</source><translation>Conversion failed</translation></message>
        <message><source>无法启动 %1：%2</source><translation>Could not start %1: %2</translation></message>
        <message><source>转换已取消。</source><translation>Conversion canceled.</translation></message>
        <message><source>%1 返回了错误。
%2</source><translation>%1 returned an error.
%2</translation></message>
        <message><source>无法写入输出文件。</source><translation>Could not write the output file.</translation></message>
        <message><source>保存转换结果失败，原文件没有改动。</source><translation>Could not save the converted result. The original file was not changed.</translation></message>
        <message><source>转换完成：%1</source><translation>Conversion complete: %1</translation></message>
        <message><source>数字证书</source><translation>Digital certificate</translation></message>
        <message><source>缺少数字签名引擎</source><translation>Digital signature engine missing</translation></message>
        <message><source>数字签名需要 pyHanko CLI。请安装 pyhanko-cli，并确保 pyhanko 位于 PATH 中。</source><translation>Digital signatures require the pyHanko CLI. Install pyhanko-cli and ensure pyhanko is on PATH.</translation></message>
        <message><source>数字证书签名</source><translation>Digital certificate signature</translation></message>
        <message><source>选择…</source><translation>Choose…</translation></message>
        <message><source>可选，例如 https://tsa.example.com</source><translation>Optional, for example https://tsa.example.com</translation></message>
        <message><source>嵌入证书链与吊销信息（PAdES-LT）</source><translation>Embed certificate chain and revocation data (PAdES-LT)</translation></message>
        <message><source>PKCS#12 证书</source><translation>PKCS#12 certificate</translation></message>
        <message><source>证书密码</source><translation>Certificate password</translation></message>
        <message><source>RFC 3161 时间戳</source><translation>RFC 3161 timestamp</translation></message>
        <message><source>选择数字证书</source><translation>Select digital certificate</translation></message>
        <message><source>PKCS#12 证书 (*.p12 *.pfx)</source><translation>PKCS#12 certificate (*.p12 *.pfx)</translation></message>
        <message><source>保存已签名文档</source><translation>Save signed document</translation></message>
        <message><source>数字签名</source><translation>Digital signature</translation></message>
        <message><source>无法创建临时密码文件。</source><translation>Could not create a temporary password file.</translation></message>
        <message><source>正在创建数字签名…</source><translation>Creating digital signature…</translation></message>
        <message><source>无法启动 pyHanko。</source><translation>Could not start pyHanko.</translation></message>
        <message><source>数字签名已取消。</source><translation>Digital signature canceled.</translation></message>
        <message><source>数字签名失败</source><translation>Digital signature failed</translation></message>
        <message><source>pyHanko 未生成签名文件。</source><translation>pyHanko did not generate a signed file.</translation></message>
        <message><source>数字签名完成：%1</source><translation>Digital signature complete: %1</translation></message>
        <message><source>选择待验证 PDF</source><translation>Select PDF to verify</translation></message>
        <message><source>无法验证签名</source><translation>Cannot verify signature</translation></message>
        <message><source>
请安装 pyhanko-cli，并确保 pyhanko 位于 PATH 中。</source><translation>
Install pyhanko-cli and ensure pyhanko is on PATH.</translation></message>
        <message><source>验证签名</source><translation>Verify signature</translation></message>
        <message><source>签名验证进程启动失败或超时。</source><translation>The signature verification process failed to start or timed out.</translation></message>
        <message><source>签名验证通过</source><translation>Signature verification passed</translation></message>
        <message><source>签名验证未通过</source><translation>Signature verification failed</translation></message>
        <message><source>没有发现有效数字签名。</source><translation>No valid digital signature was found.</translation></message>
            <message>
            <source>CSV</source>
            <translation>CSV</translation>
        </message>
        <message>
            <source>FDF</source>
            <translation>FDF</translation>
        </message>
        <message>
            <source>XFDF</source>
            <translation>XFDF</translation>
        </message>
        <message>
            <source>XFDF (*.xfdf)</source>
            <translation>XFDF (*.xfdf)</translation>
        </message>
        <message>
            <source>导入 XFDF…</source>
            <translation>Import XFDF…</translation>
        </message>
        <message>
            <source>导出 XFDF…</source>
            <translation>Export XFDF…</translation>
        </message>
        <message>
            <source>文件格式</source>
            <translation>File format</translation>
        </message>
        <message>
            <source>提示</source>
            <translation>Tooltip</translation>
        </message>
        <message>
            <source>保存表单模板</source>
            <translation>Save form template</translation>
        </message>
        <message>
            <source>套用表单模板</source>
            <translation>Apply form template</translation>
        </message>
        <message>
            <source>模板名称</source>
            <translation>Template name</translation>
        </message>
        <message>
            <source>保存模板失败：%1</source>
            <translation>Failed to save the template: %1</translation>
        </message>
        <message>
            <source>读取模板失败：%1</source>
            <translation>Failed to read the template: %1</translation>
        </message>
        <message>
            <source>还没有保存过表单模板。</source>
            <translation>No form template has been saved yet.</translation>
        </message>
        <message>
            <source>已保存模板“%1”（%2 个字段）</source>
            <translation>Template "%1" saved (%2 fields)</translation>
        </message>
        <message>
            <source>已按模板创建 %1 个字段</source>
            <translation>Created %1 fields from the template</translation>
        </message>
        <message>
            <source>已按模板创建 %1 个字段（%2 个失败：%3）</source>
            <translation>Created %1 fields from the template (%2 failed: %3)</translation>
        </message>
        <message>
            <source>当前页没有表单字段。</source>
            <translation>There are no form fields on the current page.</translation>
        </message>
        <message>
            <source>拿不到当前页的尺寸。</source>
            <translation>Cannot read the size of the current page.</translation>
        </message>
        <message>
            <source>脚本已运行，结果没有写回字段。</source>
            <translation>The script ran; the result was not written back to any field.</translation>
        </message>
        <message>
            <source>已把脚本结果写入 %1</source>
            <translation>Script result written to %1</translation>
        </message>
        <message>
            <source>已按次序计算 %1 个字段，次序已保存</source>
            <translation>Calculated %1 fields in order; the order was saved</translation>
        </message>
        <message>
            <source>已按次序计算 %1 个字段；次序未能保存：%2</source>
            <translation>Calculated %1 fields in order; the order could not be saved: %2</translation>
        </message>
        <message>
            <source>这份文档没有表单字段。</source>
            <translation>This document has no form fields.</translation>
        </message>
        <message>
            <source>书签上移</source>
            <translation>Move bookmark up</translation>
        </message>
        <message>
            <source>书签下移</source>
            <translation>Move bookmark down</translation>
        </message>
        <message>
            <source>书签升级（变成同级）</source>
            <translation>Promote bookmark (make it a sibling)</translation>
        </message>
        <message>
            <source>书签降级（变成子节点）</source>
            <translation>Demote bookmark (make it a child)</translation>
        </message>
        <message>
            <source>书签已上移</source>
            <translation>Bookmark moved up</translation>
        </message>
        <message>
            <source>书签已下移</source>
            <translation>Bookmark moved down</translation>
        </message>
        <message>
            <source>书签已升级</source>
            <translation>Bookmark promoted</translation>
        </message>
        <message>
            <source>书签已降级</source>
            <translation>Bookmark demoted</translation>
        </message>
        <message>
            <source>已经是最后一个了</source>
            <translation>Already the last one</translation>
        </message>
        <message>
            <source>已经是第一个了</source>
            <translation>Already the first one</translation>
        </message>
        <message>
            <source>这个书签已经是顶层了</source>
            <translation>This bookmark is already at the top level</translation>
        </message>
        <message>
            <source>前面没有可以当父级的书签</source>
            <translation>There is no bookmark above to act as the parent</translation>
        </message>
        <message>
            <source>选中的书签已经不在文档里了。</source>
            <translation>The selected bookmark is no longer in the document.</translation>
        </message>
        <message>
            <source>已按设计创建 %1 个字段</source>
            <translation>Created %1 fields from the design</translation>
        </message>
        <message>
            <source>已按版面创建 %1 个字段</source>
            <translation>Created %1 fields from the computed layout</translation>
        </message>
        <message>
            <source>已按版面创建 %1 个字段（%2 个失败：%3）</source>
            <translation>Created %1 fields from the computed layout (%2 failed: %3)</translation>
        </message>
        <message>
            <source>没有字段可以创建。</source>
            <translation>There are no fields to create.</translation>
        </message>
        <message>
            <source>识别无域表单</source>
            <translation>Detect field-less forms</translation>
        </message>
        <message>
            <source>从线框与标签认出可填区域，确认后建域</source>
            <translation>Detect fillable areas from ruled lines and labels, then create fields after confirmation</translation>
        </message>
        <message>
            <source>识别失败。</source>
            <translation>Detection failed.</translation>
        </message>
        <message>
            <source>这份文档里没有认出可填的线框区域。
只认轴对齐的横线、竖线与方框，而且框旁边要有标签文字。</source>
            <translation>No fillable ruled areas were detected in this document.
Only axis-aligned horizontal lines, vertical lines and boxes are recognized, and each area needs a label next to it.</translation>
        </message>
        <message>
            <source>已按识别结果创建 %1 个字段</source>
            <translation>Created %1 fields from the detected candidates</translation>
        </message>
        <message>
            <source>有字段没能建出来：%1</source>
            <translation>Some fields could not be created: %1</translation>
        </message>
        <message>
            <source>已按设计创建 %1 个字段（%2 个失败：%3）</source>
            <translation>Created %1 fields from the design (%2 failed: %3)</translation>
        </message>
        <message>
            <source>表单工具设置已保存，新建字段将使用这套外观。</source>
            <translation>Form tool settings saved; new fields will use this appearance.</translation>
        </message>
        <message>
            <source>添加文章框</source>
            <translation>Article frame</translation>
        </message>
        <message>
            <source>交叉引用</source>
            <translation>Cross-reference</translation>
        </message>
        <message>
            <source>统计</source>
            <translation>Statistics</translation>
        </message>
        <message>
            <source>替代文本</source>
            <translation>Alternative text</translation>
        </message>
        <message>
            <source>给读屏软件念的说明（留空表示清除）:</source>
            <translation>Description for screen readers (leave empty to clear):</translation>
        </message>
        <message>
            <source>先在批注面板里选中一条批注</source>
            <translation>Select an annotation in the annotation panel first</translation>
        </message>
        <message>
            <source>替代文本没有写进去</source>
            <translation>The alternative text could not be written</translation>
        </message>
        <message>
            <source>已设置替代文本</source>
            <translation>Alternative text set</translation>
        </message>
        <message>
            <source>已经是本级的最后一个了</source>
            <translation>It is already the last one at this level</translation>
        </message>
        <message>
            <source>已经是本级的第一个了</source>
            <translation>It is already the first one at this level</translation>
        </message>
        <message>
            <source>朗读</source>
            <translation>Read aloud</translation>
        </message>
    <message><source>阅读配色</source><translation>Reading colors</translation></message>
    <message><source>配色:</source><translation>Colors:</translation></message>
    <message><source>夜间</source><translation>Night</translation></message>
    <message><source>护眼</source><translation>Eye care</translation></message>
    <message><source>高对比</source><translation>High contrast</translation></message>
    <message><source>阅读配色已切换</source><translation>Reading colors changed</translation></message>
    <message><source>视图模式</source><translation>View Modes</translation></message>
    <message><source>标尺单位</source><translation>Ruler Unit</translation></message>
    <message><source>选择标尺单位</source><translation>Choose the ruler unit</translation></message>
    <message><source>点</source><translation>Points</translation></message>
    <message><source>毫米</source><translation>Millimeters</translation></message>
    <message><source>英寸</source><translation>Inches</translation></message>
    <message><source>翻页过渡</source><translation>Page Transition</translation></message>
    <message><source>选择翻页过渡方式</source><translation>Choose the page transition</translation></message>
    <message><source>淡入淡出</source><translation>Fade</translation></message>
    <message><source>左滑</source><translation>Slide Left</translation></message>
    <message><source>右滑</source><translation>Slide Right</translation></message>
    <message><source>纯文本</source><translation>Plain Text</translation></message>
    <message><source>%1 KB</source><translation>%1 KB</translation></message>
    <message><source>%1 MB</source><translation>%1 MB</translation></message>
    <message><source>%1 字节</source><translation>%1 bytes</translation></message>
    <message><source>%1 条</source><translation>%1 item(s)</translation></message>
    <message><source>%1. %2</source><translation>%1. %2</translation></message>
    <message><source>%1：%2</source><translation>%1: %2</translation></message>
    <message><source>AI 内容检测</source><translation>AI Content Detection</translation></message>
    <message><source>AI 工具</source><translation>AI Tools</translation></message>
    <message><source>AI 总结配图</source><translation>AI Summary Illustrations</translation></message>
    <message><source>AI 服务还没连上。</source><translation>The AI service is not connected yet.</translation></message>
    <message><source>AI 生成 PPT</source><translation>AI-Generated PPT</translation></message>
    <message><source>AI 生成图</source><translation>AI-generated image</translation></message>
    <message><source>AI 生成图片</source><translation>AI-Generated Image</translation></message>
    <message><source>DXF 文件 (*.dxf)</source><translation>DXF files (*.dxf)</translation></message>
    <message><source>HTML 导出：包含文字、矢量图形（内嵌 SVG，%1 条）与内嵌图片（%2 张）；字号优先用内容流里的真实字号，取不到的那几行按字形高度估算。</source><translation>HTML export: text, vector graphics (inline SVG, %1 paths) and embedded images (%2 images) are included. Font sizes come from the content stream where available; the remaining lines are estimated from glyph height.</translation></message>
    <message><source>HTML 文件 (*.html)</source><translation>HTML files (*.html)</translation></message>
    <message><source>Markdown 文件 (*.md)</source><translation>Markdown files (*.md)</translation></message>
    <message><source>OCR 疑似错误</source><translation>OCR Suspected Errors</translation></message>
    <message><source>OFD 导出：文字、矢量图形与内嵌图片都带上（%1 个矢量图形、%2 张图片）；坐标单位是毫米，签章与注释不在导出范围内。</source><translation>OFD export: text, vector graphics and embedded images are included (%1 vector shapes, %2 images); coordinates are in millimeters, and signatures and annotations are out of scope.</translation></message>
    <message><source>OFD 文件 (*.ofd)</source><translation>OFD files (*.ofd)</translation></message>
    <message><source>PDF 文件包</source><translation>PDF Portfolio</translation></message>
    <message><source>PDF 转曲</source><translation>Flatten PDF Fonts</translation></message>
    <message><source>PPT 状态</source><translation>PPT Status</translation></message>
    <message><source>PPT 预览</source><translation>PPT Preview</translation></message>
    <message><source>Word 文档</source><translation>Word Document</translation></message>
    <message><source>Word 文档 (*.docx)</source><translation>Word documents (*.docx)</translation></message>
    <message><source>上下文</source><translation>Context</translation></message>
    <message><source>不可送印</source><translation>Not ready to print</translation></message>
    <message><source>不缩放</source><translation>Do Not Scale</translation></message>
    <message><source>不要出现什么，可留空</source><translation>What should not appear (optional)</translation></message>
    <message><source>主题:</source><translation>Topic:</translation></message>
    <message><source>任务名</source><translation>Job Name</translation></message>
    <message><source>任务名:</source><translation>Job name:</translation></message>
    <message><source>份数</source><translation>Copies</translation></message>
    <message><source>份数:</source><translation>Copies:</translation></message>
    <message><source>会计计算器</source><translation>Accounting Calculator</translation></message>
    <message><source>作者</source><translation>Author</translation></message>
    <message><source>作者:</source><translation>Author:</translation></message>
    <message><source>你是一位专业的演示文稿作者。只输出 JSON，不要任何解释文字。</source><translation>You are a professional presentation author. Output JSON only, with no explanatory text.</translation></message>
    <message><source>你是一位文本来源鉴定专家。只输出 JSON，不要任何解释文字。</source><translation>You are an expert in identifying the source of a text. Output JSON only, with no explanatory text.</translation></message>
    <message><source>例如 100+10%</source><translation>e.g. 100+10%</translation></message>
    <message><source>例如「偏商务风格」，可留空</source><translation>e.g. &quot;business style&quot; (optional)</translation></message>
    <message><source>例如「水彩插画」，可留空</source><translation>e.g. &quot;watercolor illustration&quot; (optional)</translation></message>
    <message><source>例如「面向新手」，可留空</source><translation>e.g. &quot;for beginners&quot; (optional)</translation></message>
    <message><source>保存文件包</source><translation>Save Portfolio</translation></message>
    <message><source>保存模板</source><translation>Save Template</translation></message>
    <message><source>先打开一个文件包。</source><translation>Open a portfolio first.</translation></message>
    <message><source>先生成一套 PPT。</source><translation>Generate a deck first.</translation></message>
    <message><source>先生成一张图。</source><translation>Generate an image first.</translation></message>
    <message><source>先选中一个条目。</source><translation>Select an entry first.</translation></message>
    <message><source>先选中一个模板。</source><translation>Select a template first.</translation></message>
    <message><source>共 %1 个打印任务 · %2</source><translation>%1 print job(s) - %2</translation></message>
    <message><source>共 %1 页。</source><translation>%1 page(s) in total.</translation></message>
    <message><source>内容来源</source><translation>Content Source</translation></message>
    <message><source>写不了这个文件：%1</source><translation>Could not write this file: %1</translation></message>
    <message><source>写入失败：%1</source><translation>Write failed: %1</translation></message>
    <message><source>分类:</source><translation>Category:</translation></message>
    <message><source>创建</source><translation>Create</translation></message>
    <message><source>创建文件包</source><translation>Create Portfolio</translation></message>
    <message><source>删除失败：模板可能已经不在了。</source><translation>Delete failed: the template may already be gone.</translation></message>
    <message><source>删除模板</source><translation>Delete Template</translation></message>
    <message><source>判定：%1 · 置信度 %2% —— %3</source><translation>Verdict: %1 - confidence %2% - %3</translation></message>
    <message><source>判定：无法判断 —— %1</source><translation>Verdict: cannot be determined - %1</translation></message>
    <message><source>包内条目</source><translation>Entries</translation></message>
    <message><source>包标题</source><translation>Portfolio Title</translation></message>
    <message><source>包里有 %1 个条目。</source><translation>The portfolio contains %1 entry(ies).</translation></message>
    <message><source>印前检查</source><translation>Preflight Check</translation></message>
    <message><source>印前检查结论</source><translation>Preflight Verdict</translation></message>
    <message><source>双面打印</source><translation>Double-sided Printing</translation></message>
    <message><source>反向提示词</source><translation>Negative Prompt</translation></message>
    <message><source>反向提示词:</source><translation>Negative prompt:</translation></message>
    <message><source>发现 %1 处疑似错误。这些只是提示，文档里的文字不会被自动改写。</source><translation>Found %1 suspected error(s). These are only hints; the text in the document is not rewritten automatically.</translation></message>
    <message><source>取出条目</source><translation>Extract Entry</translation></message>
    <message><source>取出选中条目</source><translation>Extract Selected Entry</translation></message>
    <message><source>可以送印</source><translation>Ready to print</translation></message>
    <message><source>合并成一份打印</source><translation>Combine into one print job</translation></message>
    <message><source>合并输出路径</source><translation>Combined Output Path</translation></message>
    <message><source>合并输出路径:</source><translation>Combined output path:</translation></message>
    <message><source>名字</source><translation>Name</translation></message>
    <message><source>图像化 PDF：每一页变成一张位图。页面尺寸不变，文字默认不能再选中（勾选保留文字层时可以搜索），图片按 JPEG 嵌入。</source><translation>Image-based PDF: every page becomes a bitmap. The page size is unchanged; text is no longer selectable by default (it stays searchable when the text layer is kept), and images are embedded as JPEG.</translation></message>
    <message><source>图像服务返回了 %1：%2</source><translation>The image service returned %1: %2</translation></message>
    <message><source>图片</source><translation>Image</translation></message>
    <message><source>大小</source><translation>Size</translation></message>
    <message><source>大纲</source><translation>Outline</translation></message>
    <message><source>大纲（按大纲生成时用，顶格行是页标题，缩进行是要点）:</source><translation>Outline (used when generating from an outline; unindented lines are page titles, indented lines are points):</translation></message>
    <message><source>套用</source><translation>Apply</translation></message>
    <message><source>套用模板</source><translation>Apply Template</translation></message>
    <message><source>存为 Markdown</source><translation>Save as Markdown</translation></message>
    <message><source>导出 CAD 文件</source><translation>Export CAD File</translation></message>
    <message><source>导出 HTML</source><translation>Export HTML</translation></message>
    <message><source>导出到 MS Office</source><translation>Export to MS Office</translation></message>
    <message><source>导出到 OFD</source><translation>Export to OFD</translation></message>
    <message><source>导出图像化 PDF</source><translation>Export Image-based PDF</translation></message>
    <message><source>导出失败。</source><translation>Export failed.</translation></message>
    <message><source>尺寸</source><translation>Size</translation></message>
    <message><source>尺寸:</source><translation>Size:</translation></message>
    <message><source>工具箱</source><translation>Toolbox</translation></message>
    <message><source>已保存：%1</source><translation>Saved: %1</translation></message>
    <message><source>已创建文件包：%1</source><translation>Portfolio created: %1</translation></message>
    <message><source>已取出条目：%1</source><translation>Entry extracted: %1</translation></message>
    <message><source>已按模板打印：%1。</source><translation>Printed with the template: %1.</translation></message>
    <message><source>已新建空白文档。</source><translation>A blank document was created.</translation></message>
    <message><source>已生成 %1 张。</source><translation>%1 image(s) generated.</translation></message>
    <message><source>幻灯片列表</source><translation>Slide List</translation></message>
    <message><source>建议的字</source><translation>Suggested Character</translation></message>
    <message><source>张数</source><translation>Count</translation></message>
    <message><source>张数:</source><translation>Count:</translation></message>
    <message><source>彩色打印</source><translation>Color Printing</translation></message>
    <message><source>待打包 %1 个文件。</source><translation>%1 file(s) to be packaged.</translation></message>
    <message><source>待打包文件</source><translation>Files to Package</translation></message>
    <message><source>待打印文件</source><translation>Files to Print</translation></message>
    <message><source>总页数：%1</source><translation>Total pages: %1</translation></message>
    <message><source>总页数：未知</source><translation>Total pages: unknown</translation></message>
    <message><source>打印了 %1 个任务，%2 个失败：
%3</source><translation>Printed %1 job(s), %2 failed:
%3</translation></message>
    <message><source>打印提醒</source><translation>Print Reminders</translation></message>
    <message><source>打印模板</source><translation>Print Template</translation></message>
    <message><source>打印模板列表</source><translation>Print Template List</translation></message>
    <message><source>打印计划</source><translation>Print Plan</translation></message>
    <message><source>打开文件包</source><translation>Open Portfolio</translation></message>
    <message><source>批量打印</source><translation>Batch Print</translation></message>
    <message><source>批量打印完成：%1 个任务。</source><translation>Batch print finished: %1 job(s).</translation></message>
    <message><source>按主题生成</source><translation>By Topic</translation></message>
    <message><source>按大纲生成</source><translation>By Outline</translation></message>
    <message><source>按文档生成</source><translation>By Document</translation></message>
    <message><source>按模板打印</source><translation>Print with Template</translation></message>
    <message><source>提示词</source><translation>Prompt</translation></message>
    <message><source>提示词:</source><translation>Prompt:</translation></message>
    <message><source>提示词太长，已经截断到 %1 个字符。</source><translation>The prompt is too long and has been truncated to %1 characters.</translation></message>
    <message><source>提示（知会）</source><translation>Note (informational)</translation></message>
    <message><source>提醒：%1</source><translation>Reminder: %1</translation></message>
    <message><source>插入位置（下面的文字就是当前的批注内容）:</source><translation>Insert position (the text below is the current annotation content):</translation></message>
    <message><source>插入到当前页</source><translation>Insert into Current Page</translation></message>
    <message><source>放完一个批注后不切回选择工具，可以连着标</source><translation>Stay on the annotation tool after placing one, so you can keep annotating</translation></message>
    <message><source>整篇</source><translation>Whole Document</translation></message>
    <message><source>文件包.pdf</source><translation>portfolio.pdf</translation></message>
    <message><source>文件包状态</source><translation>Portfolio Status</translation></message>
    <message><source>文档正文</source><translation>Document Text</translation></message>
    <message><source>文档正文（按文档生成时用）:</source><translation>Document text (used when generating from the document):</translation></message>
    <message><source>新建失败。</source><translation>Could not create the document.</translation></message>
    <message><source>新建文档</source><translation>New Document</translation></message>
    <message><source>方向</source><translation>Orientation</translation></message>
    <message><source>方向:</source><translation>Orientation:</translation></message>
    <message><source>最多页数</source><translation>Max Pages</translation></message>
    <message><source>最多页数:</source><translation>Max pages:</translation></message>
    <message><source>本次发送：一次提问</source><translation>Sent in this turn: one question</translation></message>
    <message><source>来源:</source><translation>Source:</translation></message>
    <message><source>标题</source><translation>Title</translation></message>
    <message><source>标题:</source><translation>Title:</translation></message>
    <message><source>检查与打包</source><translation>Check and Package</translation></message>
    <message><source>检测</source><translation>Detect</translation></message>
    <message><source>检测判定</source><translation>Detection Verdict</translation></message>
    <message><source>检测完成。</source><translation>Detection finished.</translation></message>
    <message><source>检测状态</source><translation>Detection Status</translation></message>
    <message><source>模型没有返回内容。</source><translation>The model returned no content.</translation></message>
    <message><source>模型调用失败。</source><translation>The model call failed.</translation></message>
    <message><source>模板名</source><translation>Template Name</translation></message>
    <message><source>模板名:</source><translation>Template name:</translation></message>
    <message><source>横向</source><translation>Landscape</translation></message>
    <message><source>正在检测…</source><translation>Detecting...</translation></message>
    <message><source>正在生成…</source><translation>Generating...</translation></message>
    <message><source>没有发现疑似错误。这些检查只是提示，不会自动改文档里的文字。</source><translation>No suspected errors found. These checks are only hints and never change the text in the document.</translation></message>
    <message><source>没有可检测的文本。</source><translation>There is no text to inspect.</translation></message>
    <message><source>没有提示项</source><translation>No info items</translation></message>
    <message><source>没有警告项</source><translation>No warnings</translation></message>
    <message><source>没有这个条目。</source><translation>No such entry.</translation></message>
    <message><source>没有配置 API 密钥，生成不了图片。请先在「AI 服务设置」里配置。</source><translation>No API key is configured, so no image can be generated. Configure one in &quot;AI Service Settings&quot; first.</translation></message>
    <message><source>没有错误项</source><translation>No errors</translation></message>
    <message><source>添加文件</source><translation>Add Files</translation></message>
    <message><source>状态</source><translation>Status</translation></message>
    <message><source>理由</source><translation>Reason</translation></message>
    <message><source>生成</source><translation>Generate</translation></message>
    <message><source>生成状态</source><translation>Generation Status</translation></message>
    <message><source>生成结果不是一张可显示的图片</source><translation>The generated result is not a displayable image</translation></message>
    <message><source>生成结果列表</source><translation>Generated Results</translation></message>
    <message><source>生成结果预览</source><translation>Generated Result Preview</translation></message>
    <message><source>生成配图计划</source><translation>Generate Illustration Plan</translation></message>
    <message><source>画什么，写清楚一点</source><translation>What to draw - be specific</translation></message>
    <message><source>画面要求</source><translation>Visual Requirements</translation></message>
    <message><source>画面要求:</source><translation>Visual requirements:</translation></message>
    <message><source>留空表示全部页</source><translation>Leave empty for all pages</translation></message>
    <message><source>留空表示全部页，例如 1-3,5</source><translation>Leave empty for all pages, e.g. 1-3,5</translation></message>
    <message><source>疑似错误列表</source><translation>Suspected Error List</translation></message>
    <message><source>疑似错误状态</source><translation>Suspected Error Status</translation></message>
    <message><source>码</source><translation>Code</translation></message>
    <message><source>移除选中</source><translation>Remove Selected</translation></message>
    <message><source>符号分类</source><translation>Symbol Category</translation></message>
    <message><source>符号插入位置</source><translation>Symbol Insert Position</translation></message>
    <message><source>符号面板</source><translation>Symbol Panel</translation></message>
    <message><source>第 %1 张（%2）</source><translation>No. %1 (%2)</translation></message>
    <message><source>第 %1 页：%2</source><translation>Page %1: %2</translation></message>
    <message><source>算式</source><translation>Expression</translation></message>
    <message><source>算式:</source><translation>Expression:</translation></message>
    <message><source>纵向</source><translation>Portrait</translation></message>
    <message><source>纸张</source><translation>Paper</translation></message>
    <message><source>纸张:</source><translation>Paper:</translation></message>
    <message><source>结果: %1 = %2</source><translation>Result: %1 = %2</translation></message>
    <message><source>缩小到可打印</source><translation>Shrink to Printable Size</translation></message>
    <message><source>缩放方式</source><translation>Scaling</translation></message>
    <message><source>缩放方式:</source><translation>Scaling:</translation></message>
    <message><source>缩放比例</source><translation>Scale</translation></message>
    <message><source>缩放比例（%）:</source><translation>Scale (%):</translation></message>
    <message><source>自定义宽度</source><translation>Custom Width</translation></message>
    <message><source>自定义宽度（点）:</source><translation>Custom width (points):</translation></message>
    <message><source>自定义纸张按下面的宽高建页，方向不生效。</source><translation>Custom paper builds the page from the width and height below; orientation does not apply.</translation></message>
    <message><source>自定义高度</source><translation>Custom Height</translation></message>
    <message><source>自定义高度（点）:</source><translation>Custom height (points):</translation></message>
    <message><source>要总结的正文</source><translation>Text to Summarize</translation></message>
    <message><source>要总结的正文:</source><translation>Text to summarize:</translation></message>
    <message><source>要收进包里的文件:</source><translation>Files to put in the portfolio:</translation></message>
    <message><source>要检测的文本</source><translation>Text to Inspect</translation></message>
    <message><source>要检测的文本:</source><translation>Text to inspect:</translation></message>
    <message><source>要点 %1 条，可以逐条生成配图。</source><translation>%1 point(s) found; you can generate an illustration for each one.</translation></message>
    <message><source>警告（应当处理）</source><translation>Warning (should be handled)</translation></message>
    <message><source>计算结果</source><translation>Result</translation></message>
    <message><source>计算错误</source><translation>Calculation Error</translation></message>
    <message><source>识别到的字</source><translation>Recognized Character</translation></message>
    <message><source>说明</source><translation>Description</translation></message>
    <message><source>读不了这个文件：%1</source><translation>Could not read this file: %1</translation></message>
    <message><source>读不到文档内容。</source><translation>Could not read the document content.</translation></message>
    <message><source>转曲失败。</source><translation>Flattening failed.</translation></message>
    <message><source>优化扫描件</source><translation>Optimize scanned pages</translation></message>
    <message><source>逐页去掉扫描噪点并把倾斜的页面摆正</source><translation>Remove scan noise page by page and straighten tilted pages</translation></message>
    <message><source>优化完成：%1 页，已去噪并纠偏，已保存：%2</source><translation>Done: %1 pages denoised and deskewed, saved to %2</translation></message>
    <message><source>优化失败。</source><translation>Optimization failed.</translation></message>
    <message><source>转曲结果：%1 个字符已转成矢量路径，%2 个字符取不到轮廓。产物里不再有字体依赖，文字不能再选中或搜索。</source><translation>Flattening result: %1 characters were converted to vector paths, %2 characters could not be outlined. The output no longer depends on fonts, and the text can no longer be selected or searched.</translation></message>
    <message><source>取不到轮廓的字符（前 %1 条，共 %2 条）：%3</source><translation>Characters that could not be outlined (first %1 of %2): %3</translation></message>
    <message><source>还没有文件。</source><translation>No file yet.</translation></message>
    <message><source>还没有生成结果</source><translation>No generated result yet</translation></message>
    <message><source>这一页没有可导出的文字层。</source><translation>This page has no exportable text layer.</translation></message>
    <message><source>这次 PPT 讲什么</source><translation>What should this deck cover?</translation></message>
    <message><source>这次请求被取消了。</source><translation>This request was cancelled.</translation></message>
    <message><source>连续批注</source><translation>Continuous Annotation</translation></message>
    <message><source>适应页面</source><translation>Fit to Page</translation></message>
    <message><source>选择要打印的文件</source><translation>Choose the files to print</translation></message>
    <message><source>选择要收进包里的文件</source><translation>Choose the files to put in the portfolio</translation></message>
    <message><source>配图状态</source><translation>Illustration Status</translation></message>
    <message><source>配图计划</source><translation>Illustration Plan</translation></message>
    <message><source>配图计划:</source><translation>Illustration plan:</translation></message>
    <message><source>错误（必须处理）</source><translation>Error (must be handled)</translation></message>
    <message><source>队列里没有任务。</source><translation>There is no job in the queue.</translation></message>
    <message><source>附加要求</source><translation>Extra Requirements</translation></message>
    <message><source>附加要求:</source><translation>Extra requirements:</translation></message>
    <message><source>页数</source><translation>Pages</translation></message>
    <message><source>页数:</source><translation>Pages:</translation></message>
    <message><source>页码</source><translation>Page Number</translation></message>
    <message><source>页码范围</source><translation>Page Range</translation></message>
    <message><source>页码范围:</source><translation>Page range:</translation></message>
    <message><source>页码范围写错了：%1</source><translation>The page range is invalid: %1</translation></message>
    <message><source>风格</source><translation>Style</translation></message>
    <message><source>风格:</source><translation>Style:</translation></message>
    <message><source>新建</source><translation>New</translation></message>
    <message><source>文生图</source><translation>Text to Image</translation></message>
    <message><source>总结配图</source><translation>Summary Illustrations</translation></message>
    <message><source>内容检测</source><translation>Content Detection</translation></message>
    <message><source>智能朗读</source><translation>Smart Read-Aloud</translation></message>
    <message><source>计算器</source><translation>Calculator</translation></message>
    <message><source>新建一份空白文档（可选纸张、方向与页数）</source><translation>Create a blank document (paper size, orientation and page count are optional)</translation></message>
    <message><source>把多个 PDF 排成一个打印队列</source><translation>Queue several PDF files for printing</translation></message>
    <message><source>保存与套用打印参数（份数、纸张、缩放、页范围）</source><translation>Save and reuse print settings (copies, paper, scaling, page range)</translation></message>
    <message><source>用一句提示词生成图片，并插到当前页</source><translation>Generate an image from one prompt and insert it into the current page</translation></message>
    <message><source>把正文压成要点，再逐条生成配图</source><translation>Condense the text into key points, then illustrate them one by one</translation></message>
    <message><source>按主题、文档或大纲生成一套 PPT</source><translation>Generate a deck from a topic, the document, or an outline</translation></message>
    <message><source>判断一段文字是不是 AI 写的</source><translation>Judge whether a piece of text was written by AI</translation></message>
    <message><source>清洗正文、排成朗读分段并逐段朗读</source><translation>Clean up the text, split it into read-aloud segments and read them one by one</translation></message>
    <message><source>算一条算式，把结果整理成批注插到当前页</source><translation>Evaluate an expression and insert the result as an annotation on the current page</translation></message>
    <message><source>按分类挑符号，插到当前光标处</source><translation>Pick a symbol by category and insert it at the cursor</translation></message>
    <message><source>把整篇文档导成一个 HTML 文件：文字、矢量图形与内嵌图片都带上</source><translation>Export the whole document as one HTML file: text, vector graphics and embedded images included</translation></message>
    <message><source>把每一页渲染成位图，写成一份新的 PDF</source><translation>Render every page as a bitmap and write a new PDF</translation></message>
    <message><source>把文字、矢量图形与内嵌图片一起导成 OFD 包</source><translation>Export text, vector graphics and embedded images into an OFD package</translation></message>
    <message><source>给整篇文档写一棵语义标签树（标题 / 段落 / 列表 / 表格 / 图 / 表单域）</source><translation>Write a semantic tag tree for the whole document (headings / paragraphs / lists / tables / figures / form fields)</translation></message>
    <message><source>把整篇文档导成 Word / Excel / PPT（含图片与表格）</source><translation>Export the whole document as Word / Excel / PPT (with images and tables)</translation></message>
    <message><source>把当前页的文字层导成 Word 文档</source><translation>Export the text layer of the current page as a Word document</translation></message>
    <message><source>把文字转成矢量路径：产物里没有字体依赖，文字不能再选中或搜索</source><translation>Turn the text into vector paths: the output no longer depends on fonts, and the text can no longer be selected or searched</translation></message>
    <message><source>检查字体内嵌、图片分辨率、脚本等印前问题</source><translation>Check font embedding, image resolution, scripts and other preflight problems</translation></message>
    <message><source>把若干 PDF 收进一个活页夹，或打开一个包</source><translation>Collect several PDF files into a portfolio, or open one</translation></message>
    <message><source>列出 OCR 认错的字（只提示，不自动改）</source><translation>List characters OCR got wrong (hints only, nothing is changed automatically)</translation></message>
    <message><source>图像化 PDF</source><translation>Image-based PDF</translation></message>
    <message><source>生成 PPT</source><translation>Generate PPT</translation></message>
    <message><source>到 OFD</source><translation>Export to OFD</translation></message>
    <message><source>到 MS Office</source><translation>Export to MS Office</translation></message>
    <message><source>到 CAD</source><translation>Export to CAD</translation></message>
    <message><source>AI 面板还没打开。</source><translation>The AI panel is not open yet.</translation></message>
    <message><source>PDF/UA 合规检查</source><translation>PDF/UA compliance check</translation></message>
    <message><source>XFA 字段</source><translation>XFA fields</translation></message>
    <message><source>从文件新建 PDF</source><translation>New PDF from a file</translation></message>
    <message><source>写入失败，文档没有修改。</source><translation>Writing failed; the document was not changed.</translation></message>
    <message><source>写入隐藏文字层</source><translation>Write invisible text layer</translation></message>
    <message><source>写回图层失败，文档没有修改。</source><translation>Writing the layers back failed; the document was not changed.</translation></message>
    <message><source>动作向导</source><translation>Action wizard</translation></message>
    <message><source>发票打印</source><translation>Invoice printing</translation></message>
    <message><source>发票还没填完整。</source><translation>The invoice is not filled in yet.</translation></message>
    <message><source>图层面板</source><translation>Layer panel</translation></message>
    <message><source>已写入注解。</source><translation>The annotation was written.</translation></message>
    <message><source>已写回图层设置。</source><translation>The layer settings were written back.</translation></message>
    <message><source>已导出：%1</source><translation>Exported: %1</translation></message>
    <message><source>已送到打印机。</source><translation>Sent to the printer.</translation></message>
    <message><source>打印机没有接受这次打印。</source><translation>The printer did not accept this print job.</translation></message>
    <message><source>按新页宽重排</source><translation>Reflow to a new page width</translation></message>
    <message><source>插入 3D 模型 / 多媒体</source><translation>Insert 3D model / media</translation></message>
    <message><source>没有可以打开文件的入口。</source><translation>There is no way to open a file here.</translation></message>
    <message><source>识别控制器不可用。</source><translation>The recognition controller is not available.</translation></message>
    <message><source>这一步没有文件路径。</source><translation>This step has no file path.</translation></message>
    <message><source>这个文件是空的：%1</source><translation>This file is empty: %1</translation></message>
    <message><source>这份文档还没有识别结果：先在页面上点「识别」（OCR），再回来写隐藏文字层。</source><translation>This document has no recognition results yet: run &quot;Recognise&quot; (OCR) on the page first, then come back and write the invisible text layer.</translation></message>
    <message><source>重排完成：%1 页。</source><translation>Reflow done: %1 page(s).</translation></message>
    <message><source>把 txt / md / csv / json / html 转成 PDF</source><translation>Convert txt / md / csv / json / html into a PDF</translation></message>
    <message><source>把整理、加页码、加水印等操作编排成一条流程</source><translation>Sequence existing document operations into one recipe</translation></message>
    <message><source>填发票要素、预览版式，再打印或导出 PDF</source><translation>Fill in the invoice details, preview the layout, then print or export a PDF</translation></message>
    <message><source>把识别结果写成不可见文字层（可搜索、可复制）</source><translation>Write the recognition result as an invisible text layer (searchable, copyable)</translation></message>
    <message><source>段落排版</source><translation>Paragraph formatting</translation></message>
    <message><source>按对齐、行距与缩进重排选中的段落</source><translation>Re-lay out the selected paragraph with alignment, line spacing and indents</translation></message>
    <message><source>插入 3D 模型</source><translation>Insert 3D model</translation></message>
    <message><source>插入一个 3D 模型注解（U3D / PRC 文件）</source><translation>Insert a 3D model annotation (U3D / PRC file)</translation></message>
    <message><source>查看 3D 模型</source><translation>View 3D model</translation></message>
    <message><source>渲染文档里的 3D 注解，拖动可旋转、滚轮可缩放</source><translation>Render the 3D annotations in the document; drag to orbit and use the wheel to zoom</translation></message>
    <message><source>3D 模型</source><translation>3D model</translation></message>
    <message><source>这份文档里没有 3D 注解。</source><translation>This document has no 3D annotations.</translation></message>
    <message><source>插入多媒体</source><translation>Insert media</translation></message>
    <message><source>按 PDF/UA 逐项检查并给出修复建议</source><translation>Check item by item against PDF/UA and suggest fixes</translation></message>
    <message><source>列出 XFA 表单字段并导出成 CSV</source><translation>List the XFA form fields and export them as CSV</translation></message>
    <message><source>列出文档里的图层，逐个显示或隐藏</source><translation>List the layers of the document and show or hide each one</translation></message>
    <message><source>按新的页宽把正文重新排一遍（版面会变）</source><translation>Re-lay out the body text at a new page width (the layout changes)</translation></message>
    <message><source>本地离线问答</source><translation>Local offline Q&amp;A</translation></message>
    <message><source>在本地索引里检索并摘出答案（不联网、不调模型）</source><translation>Search the local index and extract the answer (no network, no model)</translation></message>
    <message><source>读取标签树</source><translation>Read tag tree</translation></message>
    <message><source>读出文档里的标签树（标签、页码、层级、替代文本与语言）</source><translation>Read the tag tree of the document (tags, pages, depth, alternative text and language)</translation></message>
            <message>
                <source>切换界面语言</source>
                <translation>Change interface language</translation>
            </message>
            <message>
                <source>从剪贴板新建 PDF</source>
                <translation>New PDF from Clipboard</translation>
            </message>
            <message>
                <source>没能从剪贴板生成 PDF。</source>
                <translation>Could not create a PDF from the clipboard.</translation>
            </message>
            <message>
                <source>从网页新建 PDF</source>
                <translation>New PDF from Web Page</translation>
            </message>
            <message>
                <source>从表单文件新建 PDF</source>
                <translation>New PDF from Form Data File</translation>
            </message>
            <message>
                <source>从扫描仪新建 PDF</source>
                <translation>New PDF from Scanner</translation>
            </message>
            <message>
                <source>没有扫到任何页面。</source>
                <translation>No pages were scanned.</translation>
            </message>
            <message>
                <source>扫描完成：%1 页，已保存：%2</source>
                <translation>Scan finished: %1 page(s), saved to %2</translation>
            </message>
            <message>
                <source>文档比较</source>
                <translation>Compare Documents</translation>
            </message>
            <message>
                <source>批量加密</source>
                <translation>Batch Encrypt</translation>
            </message>
            <message>
                <source>选择要批量加密的目录</source>
                <translation>Choose the folder to encrypt</translation>
            </message>
            <message>
                <source>选择加密后的输出目录</source>
                <translation>Choose the output folder for the encrypted files</translation>
            </message>
            <message>
                <source>这个目录里没有 PDF。</source>
                <translation>There is no PDF in this folder.</translation>
            </message>
            <message>
                <source>打开密码不能为空。</source>
                <translation>The open password cannot be empty.</translation>
            </message>
            <message>
                <source>加密</source>
                <translation>Encrypt</translation>
            </message>
            <message>
                <source>批量加密完成：成功 %1，失败 %2。失败原因见输出目录里的报告。</source>
                <translation>Batch encryption finished: %1 succeeded, %2 failed. See the report in the output folder for the reasons.</translation>
            </message>
            <message>
                <source>批量加密完成：成功 %1，失败 %2。</source>
                <translation>Batch encryption finished: %1 succeeded, %2 failed.</translation>
            </message>
            <message>
                <source>扫描</source>
                <translation>Scan</translation>
            </message>
            <message>
                <source>从扫描仪</source>
                <translation>From Scanner</translation>
            </message>
            <message>
                <source>用扫描仪取图并生成多页 PDF</source>
                <translation>Capture images with a scanner and build a multi-page PDF</translation>
            </message>
            <message>
                <source>比较</source>
                <translation>Compare</translation>
            </message>
            <message>
                <source>比较两份 PDF 的文字差异</source>
                <translation>Compare the text differences between two PDFs</translation>
            </message>
            <message>
                <source>给一个目录里的 PDF 批量加打开密码</source>
                <translation>Add an open password to every PDF in a folder</translation>
            </message>
            <message>
                <source>输入</source>
                <translation>Input</translation>
            </message>
            <message>
                <source>从剪贴板</source>
                <translation>From Clipboard</translation>
            </message>
            <message>
                <source>把剪贴板里的文字变成一份新 PDF</source>
                <translation>Turn the text in the clipboard into a new PDF</translation>
            </message>
            <message>
                <source>从网页</source>
                <translation>From Web Page</translation>
            </message>
            <message>
                <source>用模板 PDF 加表单数据文件生成 PDF</source>
                <translation>Fill a template PDF with a form data file</translation>
            </message>
            <message>
                <source>循环放映</source>
                <translation>Loop Presentation</translation>
            </message>
            <message>
                <source>放映到最后一页时自动回到第一页</source>
                <translation>Go back to the first page after the last one</translation>
            </message>
            <message>
                <source>循环放映已打开：放映到最后一页会回到第一页。</source>
                <translation>Loop presentation is on: the presentation returns to the first page after the last one.</translation>
            </message>
            <message>
                <source>循环放映已关闭：放映到最后一页会停住。</source>
                <translation>Loop presentation is off: the presentation stops at the last page.</translation>
            </message>
            <message>
                <source>自动添加表单域标签</source>
                <translation>Auto-tag Form Fields</translation>
            </message>
            <message>
                <source>给表单域写标签树与替代文本，让读屏软件能念出来</source>
                <translation>Write a tag tree and alternative text for form fields so screen readers can read them</translation>
            </message>
            <message>
                <source>已给 %1 个表单域加上标签。</source>
                <translation>Tagged %1 form field(s).</translation>
            </message>
            <message>
                <source>发送到 OneNote</source>
                <translation>Send to OneNote</translation>
            </message>
            <message>
                <source>当前文档</source>
                <translation>Current document</translation>
            </message>
            <message>
                <source>这份文档没有可送出的页面。</source>
                <translation>There is no page in this document to send.</translation>
            </message>
            <message>
                <source>把整篇文档的每一页送进本机 OneNote</source>
                <translation>Send every page of the document to the OneNote app on this computer</translation>
            </message>
            <message>
                <source>文档本来就有标签树，这次是合并进去的，保留了 %1 个既有元素。</source>
                <translation>The document already had a tag tree; this run merged into it and kept %1 existing element(s).</translation>
            </message>
            <message>
                <source>保留下来的元素类型：%1</source>
                <translation>Preserved element types: %1</translation>
            </message>
            <message>
                <source>这份文档本来没有标签树，这次新建了一棵。</source>
                <translation>This document had no tag tree; a new one was created.</translation>
            </message>
            <message>
                <source>PDF/A-2b 预检</source>
                <translation>PDF/A-2b Preflight</translation>
            </message>
            <message>
                <source>逐项检查离 PDF/A-2b 还差什么（预检，不是合规认证）</source>
                <translation>Check item by item what is still missing for PDF/A-2b (preflight, not certification)</translation>
            </message>
        <message>
            <source>安装语言包…</source>
            <translation>Install Language Pack…</translation>
        </message>
        <message>
            <source>存为 PPTX</source>
            <translation>Save as PPTX</translation>
        </message>
        <message>
            <source>PowerPoint 演示文稿 (*.pptx)</source>
            <translation>PowerPoint presentation (*.pptx)</translation>
        </message>
        <message>
            <source>导出 PPTX 失败：这一套 PPT 没有可导出的内容。</source>
            <translation>Failed to export the PPTX: this deck has nothing to export.</translation>
        </message>
        <message>
            <source>Excel 工作簿 (*.xlsx);;CSV (*.csv)</source>
            <translation>Excel workbook (*.xlsx);;CSV (*.csv)</translation>
        </message>
        <message>
            <source>把字段和值导出为 Excel 工作簿（.xlsx）</source>
            <translation>Export the field names and values as an Excel workbook (.xlsx)</translation>
        </message>
        <message>
            <source>「%1」放完后保持选中。</source>
            <translation>“%1” stays selected after placing.</translation>
        </message>
        <message>
            <source>「%1」放完后回到选择工具。</source>
            <translation>“%1” returns to the select tool after placing.</translation>
        </message>
        <message>
            <source>保持工具选择</source>
            <translation>Keep tool selected</translation>
        </message>
        <message>
            <source>先选中一个批注工具，再设置「保持工具选择」。</source>
            <translation>Select an annotation tool first, then set Keep tool selected.</translation>
        </message>
        <message>
            <source>全选结果</source>
            <translation>Select all matches</translation>
        </message>
        <message>
            <source>全部类型</source>
            <translation>All types</translation>
        </message>
        <message>
            <source>已删除 %1 条批注</source>
            <translation>Deleted %1 annotations</translation>
        </message>
        <message>
            <source>已选中 %1 条批注</source>
            <translation>Selected %1 annotations</translation>
        </message>
        <message>
            <source>当前选中的批注工具放完后保持激活，可以连着放</source>
            <translation>The selected annotation tool stays active after placing, so you can keep going</translation>
        </message>
        <message>
            <source>正文或作者</source>
            <translation>Contents or author</translation>
        </message>
        <message>
            <source>没有匹配的批注</source>
            <translation>No matching annotations</translation>
        </message>
        <message>
            <source>筛选</source>
            <translation>Filter</translation>
        </message>
        <message>
            <source>数字图章</source>
            <translation>Number stamp</translation>
        </message>
        <message>
            <source>放自动编号的图章（数字或字母）</source>
            <translation>Place a stamp with an automatic number (digits or letters)</translation>
        </message>
        <message>
            <source>阅读顺序预览与朗读设置（音色、语速、音量）</source>
            <translation>Reading order preview and narration settings (voice, rate, volume)</translation>
        </message>
        <message>
            <source>区域框选</source>
            <translation>Select a region</translation>
        </message>
        <message>
            <source>整个窗口</source>
            <translation>Whole window</translation>
        </message>
        <message>
            <source>截图范围:</source>
            <translation>Screenshot area:</translation>
        </message>
        <message>
            <source>已取消截图</source>
            <translation>Screenshot cancelled</translation>
        </message>
        <message>
            <source>沉浸阅读</source>
            <translation>Immersive reading</translation>
        </message>
        <message>
            <source>隐藏界面进入全屏阅读，按 Esc 退出</source>
            <translation>Hide the interface for full-screen reading; press Esc to exit</translation>
        </message>
        <message>
            <source>沉浸阅读：按 Esc 退出</source>
            <translation>Immersive reading: press Esc to exit</translation>
        </message>
        <message>
            <source>截图</source>
            <translation>Screenshot</translation>
        </message>
        <message>
            <source>已应用无障碍配置。</source>
            <translation>Accessibility settings applied.</translation>
        </message>
        <message>
            <source>手型</source>
            <translation>Hand</translation>
        </message>
        <message>
            <source>按住左键拖动，平移视图</source>
            <translation>Hold the left mouse button and drag to pan the view</translation>
        </message>
    <message><source>表单自动识别</source><translation>Form auto-recognition</translation></message>
    <message><source>CAD 导出：直线 %1 条、折线 %2 条、样条（真曲线）%3 条、文字 %4 条、尺寸标注 %5 个；图层 %6 个（含 PATH / TEXT / DIM），颜色与线宽一并写入；位图不导出，渐变与剪裁路径会丢失。</source><translation>CAD export: %1 lines, %2 polylines, %3 splines (true curves), %4 texts, %5 dimensions; %6 layers (PATH / TEXT / DIM), with colors and line widths; bitmaps are not exported, and gradients and clipping paths are lost.</translation></message>
    <message><source>文档里有 %1 张位图，这一版没有导出位图。</source><translation>The document has %1 bitmap(s); this version does not export bitmaps.</translation></message>
    <message><source>把页面上的矢量路径导成 DXF：曲线保形、尺寸标注单独分层、含颜色与线宽</source><translation>Export the vector paths on the page to DXF: curves keep their shape, dimensions go on their own layer, with colors and line widths</translation></message>
    <message><source>全文流式重排</source><translation>Whole-document streaming reflow</translation></message>
    <message><source>把整篇按新的正文栏宽重新流排并写回</source><translation>Reflow the whole document at a new body column width and write it back</translation></message>
    <message><source>跨文本块合并</source><translation>Merge across text blocks</translation></message>
    <message><source>把当前页上的几块文字合成一段连续文字</source><translation>Merge several text blocks on the current page into one continuous paragraph</translation></message>
    <message><source>正文栏宽（点）:</source><translation>Body column width (points):</translation></message>
    <message><source>全文流式重排完成：%1 页、%2 段里重排了 %3 段（%4 字）。</source><translation>Whole-document reflow done: %1 pages, %3 of %2 paragraphs reflowed (%4 characters).</translation></message>
    <message><source>这份文档没有结构标签，本次只按内容流里的段落逐段处理。</source><translation>This document has no structure tags; this run only handles the paragraphs in the content stream.</translation></message>
    <message><source>有 %1 段里有标准 14 号字体表示不了的字符，写成了 '?' 占位符。</source><translation>In %1 paragraph(s) some characters cannot be represented by the standard 14 fonts; they were written as '?' placeholders.</translation></message>
    <message><source>这一页上的文本块不足两块，没有可合并的东西。</source><translation>This page has fewer than two text blocks; there is nothing to merge.</translation></message>
    <message><source>合并完成：%1 块合成一段，共 %2 行（%3 字）。</source><translation>Merge done: %1 blocks became one paragraph, %2 lines in total (%3 characters).</translation></message>
    <message><source>块与块之间有图片或表格，合并后的位置可能与原位置有出入。</source><translation>Images or tables sit between the blocks, so the merged text may sit slightly off its old position.</translation></message>
    <message><source>整篇翻译</source><translation>Translate whole document</translation></message>
    <message><source>把整篇文档按段翻译并把译文写回</source><translation>Translate the whole document paragraph by paragraph and write the translations back</translation></message>
    <message><source>AI Worker 没有连上，翻译不了。</source><translation>The AI worker is not connected; translation is not possible.</translation></message>
    <message><source>当前是演示模式，翻译出来的示例不能写回文档。</source><translation>The provider is in demo mode; demo translations cannot be written back to the document.</translation></message>
    <message><source>译成哪种语言？</source><translation>Translate into which language?</translation></message>
    <message><source>整篇翻译会把这份文档的正文按段发给 AI 服务。确定继续吗？</source><translation>Translating the whole document sends its text to the AI service paragraph by paragraph. Continue?</translation></message>
    <message><source>翻译完成：%1 段里写回了 %2 段译文（%3 字），%4 段保留原文。</source><translation>Translation done: %2 of %1 paragraphs were written back (%3 characters); %4 paragraphs kept their original text.</translation></message>
</context>
        <context>
        <name>aipdf::app::MediaPreview</name>
        <message>
            <source>编码：%1</source>
            <translation>Codec: %1</translation>
        </message>
        <message>
            <source>播放</source>
            <translation>Play</translation>
        </message>
        <message>
            <source>播放进度</source>
            <translation>Playback position</translation>
        </message>
        <message>
            <source>采样率：%1 Hz</source>
            <translation>Sample rate: %1 Hz</translation>
        </message>
        <message>
            <source>打不开音频输出设备。</source>
            <translation>Could not open the audio output device.</translation>
        </message>
        <message>
            <source>打不开音频输出设备（错误码 %1）。</source>
            <translation>Could not open the audio output device (error code %1).</translation>
        </message>
        <message>
            <source>打不开这个媒体文件。</source>
            <translation>Could not open this media file.</translation>
        </message>
        <message>
            <source>还没有读到媒体信息。</source>
            <translation>Media information has not been read yet.</translation>
        </message>
        <message>
            <source>画面</source>
            <translation>Video frame</translation>
        </message>
        <message>
            <source>画面尺寸：%1 × %2</source>
            <translation>Frame size: %1 × %2</translation>
        </message>
        <message>
            <source>解码器还没有打开。</source>
            <translation>The decoder is not open.</translation>
        </message>
        <message>
            <source>解码失败，这个文件放不出内容。</source>
            <translation>Decoding failed; this file cannot be played.</translation>
        </message>
        <message>
            <source>类型：%1</source>
            <translation>Type: %1</translation>
        </message>
        <message>
            <source>媒体信息</source>
            <translation>Media information</translation>
        </message>
        <message>
            <source>声道数：%1</source>
            <translation>Channels: %1</translation>
        </message>
        <message>
            <source>声音放不出来：%1</source>
            <translation>Sound cannot be played: %1</translation>
        </message>
        <message>
            <source>时长：%1</source>
            <translation>Duration: %1</translation>
        </message>
        <message>
            <source>视频</source>
            <translation>Video</translation>
        </message>
        <message>
            <source>停止</source>
            <translation>Stop</translation>
        </message>
        <message>
            <source>写入音频数据失败（错误码 %1）。</source>
            <translation>Writing audio data failed (error code %1).</translation>
        </message>
        <message>
            <source>写入音频数据失败：播放器还没有打开。</source>
            <translation>Writing audio data failed: the player is not open.</translation>
        </message>
        <message>
            <source>音频</source>
            <translation>Audio</translation>
        </message>
        <message>
            <source>暂停</source>
            <translation>Pause</translation>
        </message>
        <message>
            <source>这不是媒体文件，里面没有可以预览的音视频内容。</source>
            <translation>This is not a media file; it contains no audio or video to preview.</translation>
        </message>
        <message>
            <source>这是音频，没有画面。</source>
            <translation>This is audio; there is no video.</translation>
        </message>
        <message>
            <source>这台机器没有音频输出设备。</source>
            <translation>This machine has no audio output device.</translation>
        </message>
        <message>
            <source>帧率：%1 帧/秒</source>
            <translation>Frame rate: %1 fps</translation>
        </message>
    </context>
<context>
        <name>aipdf::app::OcrController</name>
        <message>
            <source>取不到页面尺寸</source>
            <translation>Could not read the page size</translation>
        </message>
        <message>
            <source>本机没有可用的 OCR：可以装 tesseract（PATH 或 AIPDF_TESSERACT_PATH），或使用 Windows 自带的 OCR</source>
            <translation>OCR is not available on this machine: install tesseract (PATH or AIPDF_TESSERACT_PATH), or use the OCR built into Windows</translation>
        </message>
        <message>
            <source>没有打开的文档</source>
            <translation>No document is open</translation>
        </message>
        <message>
            <source>索引不可用，识别结果未保存</source>
            <translation>The index is unavailable; the recognition result was not saved</translation>
        </message>
        <message>
            <source>识别已取消</source>
            <translation>Recognition cancelled</translation>
        </message>
        <message>
            <source>这一页已有可用的文本层，不需要 OCR</source>
            <translation>This page already has a usable text layer; OCR is not needed</translation>
        </message>
        <message>
            <source>这一页没有识别出文字</source>
            <translation>No text was recognized on this page</translation>
        </message>
        <message>
            <source>页码超出范围</source>
            <translation>Page number is out of range</translation>
        </message>
        <message>
            <source>页面渲染失败</source>
            <translation>Failed to render the page</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::PageCanvas</name>
        <message>
            <source>未打开文档</source>
            <translation>No document open</translation>
        </message>
    
    <message><source>点击页面选择箭头指向的位置</source><translation>Click the page to choose where the arrow points</translation></message>
    <message><source>请在同一个页面上选择箭头指向的位置</source><translation>Choose the arrow target on the same page</translation></message>
    <message><source>已取消箭头框</source><translation>Callout cancelled</translation></message>
</context>
    <context>
        <name>aipdf::app::PdfView</name>
                <message>
            <source>

（显示的是标签树里每页的子元素顺序；语义树与行级树都能读，取决于这份文档最后写的是哪一棵。）</source>
            <translation>

(It shows the order of the child elements on each page of the tag tree; both semantic trees and line-level trees are readable, depending on which one was written last.)</translation>
        </message>
        <message>
            <source>导出结果是空的。</source>
            <translation>The exported result is empty.</translation>
        </message>
        <message>
            <source>导出失败。</source>
            <translation>Export failed.</translation>
        </message>
        <message>
            <source>导出 PPT 失败。</source>
            <translation>Exporting to PPT failed.</translation>
        </message>
        <message>
            <source>导出 Excel 失败。</source>
            <translation>Exporting to Excel failed.</translation>
        </message>
        <message>
            <source>导出 Word 失败。</source>
            <translation>Exporting to Word failed.</translation>
        </message>
        <message>
            <source>不支持的转换格式：%1。本程序支持 docx / xlsx / pptx / rtf / txt。</source>
            <translation>Unsupported target format: %1. This program supports docx / xlsx / pptx / rtf / txt.</translation>
        </message>
        <message>
            <source>加密结果校验失败：%1</source>
            <translation>The encrypted result failed verification: %1</translation>
        </message>
        <message>
            <source>加密失败。</source>
            <translation>Encryption failed.</translation>
        </message>
        <message>
            <source>合并这一步没有要合并的文件。</source>
            <translation>The "merge" step has no files to merge.</translation>
        </message>
        <message>
            <source>书签要指向的页码超出范围：第 %1 页，这份文档一共 %2 页。</source>
            <translation>The bookmark's page is out of range: page %1, but the document only has %2 pages.</translation>
        </message>
        <message>
            <source>书签要指向第 1 页或更后面的页，这一步写的是第 %1 页。</source>
            <translation>A bookmark must point to page 1 or later; this step wrote page %1.</translation>
        </message>
        <message>
            <source>加书签失败：%1</source>
            <translation>Adding the bookmark failed: %1</translation>
        </message>
        <message>
            <source>加书签这一步没有标题。</source>
            <translation>The "add bookmark" step has no title.</translation>
        </message>
        <message>
            <source>range 模式要至少给一个页区间（比如 1-3）。</source>
            <translation>Range mode needs at least one page range (for example 1-3).</translation>
        </message>
        <message>
            <source>count 后面要写每份文件的页数（至少 1），这一步写的是：%1</source>
            <translation>The page count after "count" must be at least 1; this step wrote: %1</translation>
        </message>
<message>
            <source>已用 %1 的第 %2 页替换第 %3 页（可撤销）</source>
            <translation>Replaced page %3 with page %2 of %1 (undoable)</translation>
        </message>

                <message>
            <source>打印失败。</source>
            <translation>Printing failed.</translation>
        </message>
        <message>
            <source>拆分文档：写成功 %1 份，失败 %2 份。失败的是：%3</source>
            <translation>Split document: wrote %1 file(s), %2 failed. The failures are: %3</translation>
        </message>
        <message>
            <source>按书签拆分至少要两个有效书签（指向同一页的算一个），这份文档只拆得出 %1 段。</source>
            <translation>Splitting by bookmarks needs at least two valid bookmarks (ones pointing to the same page count as one); this document only yields %1 segment(s).</translation>
        </message>
        <message>
            <source>拆分文档这一步没有输出目录。</source>
            <translation>The "split document" step has no output directory.</translation>
        </message>
        <message>
            <source>拆分方式只支持 range（按 ranges 给的页区间）、count 或 count:N（每 N 页一份）、outline（按顶层书签），这一步写的是：%1</source>
            <translation>The split mode must be range (use the page ranges from ranges), count or count:N (one file every N pages), or outline (by top-level bookmarks); this step wrote: %1</translation>
        </message>
<message>
            <source>提取页面</source>
            <translation>Extract pages</translation>
        </message>
                        <message>
            <source>系统里没有这台打印机：%1。</source>
            <translation>This printer does not exist on the system: %1.</translation>
        </message>
        <message>
            <source>本次构建没有启用 qpdf，写不了加密文档。</source>
            <translation>This build has no qpdf, so encrypted documents cannot be written.</translation>
        </message>
<message>
            <source>证书链结论：%1</source>
            <translation>Certificate chain conclusion: %1</translation>
        </message>
        <message>
            <source>状态未知</source>
            <translation>Status unknown</translation>
        </message>
        <message>
            <source>撤销状态</source>
            <translation>Revocation status</translation>
        </message>
        <message>
            <source>读不出当前文档：%1</source>
            <translation>Cannot read the current document: %1</translation>
        </message>
<message>
            <source>起始页:</source>
            <translation>First page:</translation>
        </message>
        <message>
            <source>结束页:</source>
            <translation>Last page:</translation>
        </message>
        <message>
            <source>提取失败。</source>
            <translation>Extraction failed.</translation>
        </message>
        <message>
            <source>已提取第 %1–%2 页</source>
            <translation>Extracted pages %1–%2</translation>
        </message>
        <message>
            <source>插入页面</source>
            <translation>Insert pages</translation>
        </message>
        <message>
            <source>插入失败。</source>
            <translation>Insert failed.</translation>
        </message>
        <message>
            <source>已插入 %1 页</source>
            <translation>Inserted %1 pages</translation>
        </message>

        <message>
            <source>PDF 文件 (*.pdf)</source>
            <translation>PDF files (*.pdf)</translation>
        </message>

        <message>
            <source>替换页面</source>
            <translation>Replace page</translation>
        </message>
        <message>
            <source>用源文件的第几页替换当前页:</source>
            <translation>Which page of the source file replaces the current page:</translation>
        </message>
        <message>
            <source>替换失败。</source>
            <translation>Replace failed.</translation>
        </message>
        <message>
            <source>已用 %1 的第 %2 页替换第 %3 页</source>
            <translation>Replaced page %3 with page %2 of %1</translation>
        </message>
        <message>
            <source>交叉合并</source>
            <translation>Cross-merge</translation>
        </message>
        <message>
            <source>合并失败。</source>
            <translation>Merge failed.</translation>
        </message>
        <message>
            <source>已与 %1 交叉合并：共 %2 页</source>
            <translation>Cross-merged with %1: %2 pages total</translation>
        </message>
        <message>
            <source>调整页面大小</source>
            <translation>Resize page</translation>
        </message>
        <message>
            <source>目标尺寸:</source>
            <translation>Target size:</translation>
        </message>
        <message>
            <source>A4（595 × 842 点）</source>
            <translation>A4 (595 × 842 pt)</translation>
        </message>
        <message>
            <source>Letter（612 × 792 点）</source>
            <translation>Letter (612 × 792 pt)</translation>
        </message>
        <message>
            <source>自定义…</source>
            <translation>Custom…</translation>
        </message>
        <message>
            <source>宽度（点）:</source>
            <translation>Width (pt):</translation>
        </message>
                <message>
            <source>这份文档没有可导出的文字层。</source>
            <translation>This document has no text layer to export.</translation>
        </message>
        <message>
            <source>转换这一步没有输出路径。</source>
            <translation>The "convert" step has no output path.</translation>
        </message>
        <message>
            <source>这份文档里没有页，没法加书签。</source>
            <translation>This document has no pages, so no bookmark can be added.</translation>
        </message>
        <message>
            <source>这一步没有拆出任何文件。</source>
            <translation>This step did not split out any file.</translation>
        </message>
        <message>
            <source>页区间 %1 超出了这份文档的页数（这份文档一共 %2 页）。</source>
            <translation>Page range %1 exceeds the page count of this document (the document has %2 pages).</translation>
        </message>
        <message>
            <source>页区间的起止写反了：%1</source>
            <translation>The start and end pages of the range are reversed: %1</translation>
        </message>
        <message>
            <source>页码从 1 开始，这一步写的是：%1</source>
            <translation>Page numbers start at 1; this step wrote: %1</translation>
        </message>
        <message>
            <source>页区间写错了：%1（要写成 1-3 或者 1 这种形式）</source>
            <translation>The page range is malformed: %1 (write it like 1-3 or 1)</translation>
        </message>
        <message>
            <source>页区间是空的：每一项要写成 1-3 或者 1 这种形式。</source>
            <translation>The page range is empty: each item must be written like 1-3 or 1.</translation>
        </message>
        <message>
            <source>输出目录不存在：%1</source>
            <translation>The output directory does not exist: %1</translation>
        </message>
<message>
            <source>高度（点）:</source>
            <translation>Height (pt):</translation>
        </message>
        <message>
            <source>调整失败。</source>
            <translation>Resizing failed.</translation>
        </message>
        <message>
            <source>已把第 %1 页调整为 %2 × %3 点</source>
            <translation>Resized page %1 to %2 × %3 pt</translation>
        </message>
        <message>
            <source>页面背景</source>
            <translation>Page background</translation>
        </message>
        <message>
            <source>背景没有写进去。</source>
            <translation>The background was not written.</translation>
        </message>
        <message>
            <source>已给第 %1 页加背景</source>
            <translation>Added a background to page %1</translation>
        </message>
        <message>
            <source>交换页面失败</source>
            <translation>Failed to swap pages</translation>
        </message>
        <message>
            <source>只有一页，不需要逆序</source>
            <translation>Only one page, nothing to reverse</translation>
        </message>
        <message>
            <source>复制页面失败</source>
            <translation>Failed to copy the page</translation>
        </message>
        <message>
            <source>已交换第 %1、%2 页</source>
            <translation>Swapped pages %1 and %2</translation>
        </message>
        <message>
            <source>已复制第 %1 页</source>
            <translation>Copied page %1</translation>
        </message>
        <message>
            <source>已把 %1 页逆序</source>
            <translation>Reversed %1 pages</translation>
        </message>
        <message>
            <source>已拆分出 %1 个文件</source>
            <translation>Split into %1 files</translation>
        </message>
        <message>
            <source>拆分文档</source>
            <translation>Split document</translation>
        </message>
        <message>
            <source>每份文件包含多少页:</source>
            <translation>Pages per file:</translation>
        </message>
        <message>
            <source>逆序失败，已停止在第 %1 页</source>
            <translation>Reverse failed at page %1</translation>
        </message>
        <message>
            <source>选择输出目录</source>
            <translation>Choose output folder</translation>
        </message>
        <message>
            <source>这是 XFA 动态表单：PDFium 没有 XFA 引擎，页面可能显示为空白。</source>
            <translation>This is an XFA dynamic form: PDFium has no XFA engine, so the page may appear blank.</translation>
        </message>
        <message>
            <source>XFA 表单</source>
            <translation>XFA form</translation>
        </message>
        <message>
            <source>前缀（仅支持西文）:</source>
            <translation>Prefix (Latin only):</translation>
        </message>
        <message>
            <source>四边各裁掉多少点（72 点 = 1 英寸）:</source>
            <translation>Points to crop on each side (72 pt = 1 inch):</translation>
        </message>
        <message>
            <source>已添加贝茨编号</source>
            <translation>Bates numbering added</translation>
        </message>
        <message>
            <source>已裁剪第 %1 页</source>
            <translation>Cropped page %1</translation>
        </message>
        <message>
            <source>编号没有写进去。</source>
            <translation>The numbers were not written.</translation>
        </message>
        <message>
            <source>裁剪范围无效。</source>
            <translation>The crop range is invalid.</translation>
        </message>
        <message>
            <source>裁剪页面</source>
            <translation>Crop page</translation>
        </message>
        <message>
            <source>贝茨编号</source>
            <translation>Bates numbering</translation>
        </message>
        <message>
            <source>贝茨编号目前只支持西文：中文需要嵌入字体，暂不支持。</source>
            <translation>Bates numbering only supports Latin text: Chinese requires an embedded font.</translation>
        </message>
        <message>
            <source>起始编号:</source>
            <translation>Starting number:</translation>
        </message>
        <message>
            <source>这是 XFA 动态表单：本程序不支持 XFA，页面内容可能显示为空白。</source>
            <translation>This is an XFA dynamic form: XFA is not supported, so the page may appear blank.</translation>
        </message>
        <message>
            <source>删除图片</source>
            <translation>Delete image</translation>
        </message>
        <message>
            <source>先框选要删除的图片。</source>
            <translation>Select the image to delete first.</translation>
        </message>
        <message>
            <source>选区里没有可以删除的图片。</source>
            <translation>There is no deletable image in the selection.</translation>
        </message>
        <message>
            <source>已删除选区内的图片</source>
            <translation>Deleted the image in the selection</translation>
        </message>
        <message>
            <source>加密与权限</source>
            <translation>Encryption and permissions</translation>
        </message>
        <message>
            <source>本次构建没有启用 qpdf，写不了加密文档。请用带 qpdf 的安装包。</source>
            <translation>This build has no qpdf, so it cannot write encrypted documents. Use a package built with qpdf.</translation>
        </message>
        <message>
            <source>保存时将使用 AES-256 加密；重新打开需要密码</source>
            <translation>The file will be saved with AES-256 encryption; reopening it will require the password</translation>
        </message>
        <message>
            <source>保存时将使用 AES-256 加密并写入权限位（打开不需要密码）</source>
            <translation>The file will be saved with AES-256 encryption and permission flags (no password needed to open)</translation>
        </message>
        <message>
            <source>已关闭加密：下次保存写明文 PDF</source>
            <translation>Encryption is off; the next save writes a plain PDF</translation>
        </message>
        <message>
            <source>FDF (*.fdf)</source>
            <translation>FDF (*.fdf)</translation>
        </message>
        <message>
            <source>导入 FDF 数据</source>
            <translation>Import FDF data</translation>
        </message>
        <message>
            <source>读不了这个文件。</source>
            <translation>Cannot read this file.</translation>
        </message>
        <message>
            <source>这份 FDF 里没有可用的字段值。</source>
            <translation>This FDF contains no usable field values.</translation>
        </message>
        <message>
            <source>从 FDF 导入了 %1 个字段</source>
            <translation>Imported %1 fields from FDF</translation>
        </message>
        <message>
            <source>导出 FDF 数据</source>
            <translation>Export FDF data</translation>
        </message>
        <message>
            <source>写不了这个文件。</source>
            <translation>Cannot write this file.</translation>
        </message>
        <message>
            <source>已导出 FDF 数据</source>
            <translation>Exported FDF data</translation>
        </message>
        <message>
            <source>录制声音批注</source>
            <translation>Record sound annotation</translation>
        </message>
        <message>
            <source>正在录音…按「确定」停止并插入批注。</source>
            <translation>Recording… press OK to stop and insert the annotation.</translation>
        </message>
        <message>
            <source>没有录到音频。</source>
            <translation>No audio was recorded.</translation>
        </message>
        <message>
            <source>批注没有创建成功。</source>
            <translation>The annotation could not be created.</translation>
        </message>
        <message>
            <source>已插入录音批注</source>
            <translation>Inserted a recorded sound annotation</translation>
        </message>
        <message>
            <source>PDF (*.pdf)</source>
            <translation>PDF (*.pdf)</translation>
        </message>
        <message>
            <source>合并 PDF</source>
            <translation>Merge PDF</translation>
        </message>
        <message>
            <source>打不开这份 PDF：%1</source>
            <translation>Cannot open this PDF: %1</translation>
        </message>
        <message>
            <source>合并失败。</source>
            <translation>Merge failed.</translation>
        </message>
        <message>
            <source>已合并 %1</source>
            <translation>Merged %1</translation>
        </message>
        <message>
            <source>抽取当前页</source>
            <translation>Extract current page</translation>
        </message>
        <message>
            <source>抽取失败。</source>
            <translation>Extraction failed.</translation>
        </message>
        <message>
            <source>写不了这个文件。</source>
            <translation>Cannot write this file.</translation>
        </message>
        <message>
            <source>已抽取第 %1 页</source>
            <translation>Extracted page %1</translation>
        </message>
        <message>
            <source>图片替换</source>
            <translation>Replace with image</translation>
        </message>
        <message>
            <source>先在页面上选中要替换的区域。</source>
            <translation>Select the region to replace first.</translation>
        </message>
        <message>
            <source>这不是一张能读的图片。</source>
            <translation>This image cannot be read.</translation>
        </message>
        <message>
            <source>图片没有写进页面。</source>
            <translation>The image was not written into the page.</translation>
        </message>
        <message>
            <source>已用图片替换选区</source>
            <translation>Replaced the selection with an image</translation>
        </message>
        <message>
            <source>替换文字</source>
            <translation>Replace text</translation>
        </message>
        <message>
            <source>先选中要替换的文字。</source>
            <translation>Select the text to replace first.</translation>
        </message>
        <message>
            <source>新文字（仅支持西文）:</source>
            <translation>New text (Latin only):</translation>
        </message>
        <message>
            <source>替换失败。</source>
            <translation>Replacement failed.</translation>
        </message>
        <message>
            <source>已替换选中文字（行级）</source>
            <translation>Replaced the selection (line level)</translation>
        </message>
        <message>
            <source>1 页</source>
            <translation>1 page</translation>
        </message>
        <message>
            <source>2 页</source>
            <translation>2 pages</translation>
        </message>
        <message>
            <source>4 页</source>
            <translation>4 pages</translation>
        </message>
        <message>
            <source>6 页</source>
            <translation>6 pages</translation>
        </message>
        <message>
            <source>9 页</source>
            <translation>9 pages</translation>
        </message>
        <message>
            <source>HTML (*.html)</source>
            <translation>HTML (*.html)</translation>
        </message>
        <message>
            <source>WAV (*.wav)</source>
            <translation>WAV (*.wav)</translation>
        </message>
        <message>
            <source>XFDF (*.xfdf)</source>
            <translation>XFDF (*.xfdf)</translation>
        </message>
        <message>
            <source>下划线</source>
            <translation>Underline</translation>
        </message>
        <message>
            <source>云线</source>
            <translation>Cloud</translation>
        </message>
        <message>
            <source>便签</source>
            <translation>Note</translation>
        </message>
        <message>
            <source>保存</source>
            <translation>Save</translation>
        </message>
        <message>
            <source>保存文档 (Ctrl+S)</source>
            <translation>Save document (Ctrl+S)</translation>
        </message>
        <message>
            <source>先打开一份 PDF。</source>
            <translation>Open a PDF first.</translation>
        </message>
        <message>
            <source>写不了这个文件: %1</source>
            <translation>Cannot write this file: %1</translation>
        </message>
        <message>
            <source>删除线</source>
            <translation>Strikethrough</translation>
        </message>
        <message>
            <source>删除页失败</source>
            <translation>Failed to delete the page</translation>
        </message>
        <message>
            <source>向右旋转</source>
            <translation>Rotate Right</translation>
        </message>
        <message>
            <source>向左旋转</source>
            <translation>Rotate Left</translation>
        </message>
        <message>
            <source>周长</source>
            <translation>Perimeter</translation>
        </message>
        <message>
            <source>图片 (*.png *.jpg *.jpeg *.bmp)</source>
            <translation>Images (*.png *.jpg *.jpeg *.bmp)</translation>
        </message>
        <message>
            <source>图片图章</source>
            <translation>Image Stamp</translation>
        </message>
        <message>
            <source>图章</source>
            <translation>Stamp</translation>
        </message>
        <message>
            <source>在页面上挂一个文件</source>
            <translation>Attach a file to the page</translation>
        </message>
        <message>
            <source>在页面上放一个便签</source>
            <translation>Place a note on the page</translation>
        </message>
        <message>
            <source>在页面上放一个文字图章</source>
            <translation>Place a text stamp on the page</translation>
        </message>
        <message>
            <source>在页面上画一个矩形</source>
            <translation>Draw a rectangle on the page</translation>
        </message>
        <message>
            <source>在页面上画一笔</source>
            <translation>Draw a stroke on the page</translation>
        </message>
        <message>
            <source>墨迹</source>
            <translation>Ink</translation>
        </message>
        <message>
            <source>声音</source>
            <translation>Sound</translation>
        </message>
        <message>
            <source>声音批注</source>
            <translation>Sound annotation</translation>
        </message>
        <message>
            <source>备注内容</source>
            <translation>Note contents</translation>
        </message>
        <message>
            <source>多边形</source>
            <translation>Polygon</translation>
        </message>
        <message>
            <source>导入了 %1 条批注</source>
            <translation>Imported %1 annotations</translation>
        </message>
        <message>
            <source>导入批注</source>
            <translation>Import annotations</translation>
        </message>
        <message>
            <source>导出图片</source>
            <translation>Export images</translation>
        </message>
        <message>
            <source>导出图片到目录</source>
            <translation>Export images to a folder</translation>
        </message>
        <message>
            <source>导出图片失败</source>
            <translation>Failed to export images</translation>
        </message>
        <message>
            <source>导出批注</source>
            <translation>Export annotations</translation>
        </message>
        <message>
            <source>导出批注摘要</source>
            <translation>Export annotation summary</translation>
        </message>
        <message>
            <source>导出文本</source>
            <translation>Export text</translation>
        </message>
        <message>
            <source>导出文本失败</source>
            <translation>Failed to export text</translation>
        </message>
        <message>
            <source>已删除第 %1 页</source>
            <translation>Deleted page %1</translation>
        </message>
        <message>
            <source>已在当前页后插入空白页</source>
            <translation>Inserted a blank page after the current page</translation>
        </message>
        <message>
            <source>已导出 %1 张到 %2</source>
            <translation>Exported %1 images to %2</translation>
        </message>
        <message>
            <source>已导出批注到 %1</source>
            <translation>Exported annotations to %1</translation>
        </message>
        <message>
            <source>已导出批注摘要</source>
            <translation>Exported the annotation summary</translation>
        </message>
        <message>
            <source>已添加%1</source>
            <translation>Added %1</translation>
        </message>
        <message>
            <source>已添加页眉页脚</source>
            <translation>Added the header and footer</translation>
        </message>
        <message>
            <source>已添加页码</source>
            <translation>Added page numbers</translation>
        </message>
        <message>
            <source>已经是最后一页了</source>
            <translation>This is the last page</translation>
        </message>
        <message>
            <source>已经是第一页了</source>
            <translation>This is the first page</translation>
        </message>
        <message>
            <source>已给每页添加水印</source>
            <translation>Added a watermark to every page</translation>
        </message>
        <message>
            <source>当前页的尺寸取不到，插入失败</source>
            <translation>Could not read the current page size; insert failed</translation>
        </message>
        <message>
            <source>打印</source>
            <translation>Print</translation>
        </message>
        <message>
            <source>打印失败</source>
            <translation>Printing failed</translation>
        </message>
        <message>
            <source>打印批注摘要</source>
            <translation>Print annotation summary</translation>
        </message>
        <message>
            <source>打印版式</source>
            <translation>Print layout</translation>
        </message>
        <message>
            <source>批注摘要</source>
            <translation>Annotation summary</translation>
        </message>
        <message>
            <source>批注摘要已发送到打印机</source>
            <translation>Annotation summary sent to the printer</translation>
        </message>
        <message>
            <source>批注没有添加成功</source>
            <translation>The annotation was not added</translation>
        </message>
        <message>
            <source>拖出一个椭圆</source>
            <translation>Drag to draw an ellipse</translation>
        </message>
        <message>
            <source>拖出一个箭头</source>
            <translation>Drag to draw an arrow</translation>
        </message>
        <message>
            <source>拖出一块并输入文字</source>
            <translation>Drag a box and enter text</translation>
        </message>
        <message>
            <source>拖出一条直线</source>
            <translation>Drag to draw a line</translation>
        </message>
        <message>
            <source>拖出两点，显示距离</source>
            <translation>Drag between two points to show the distance</translation>
        </message>
        <message>
            <source>挂一段 WAV 音频</source>
            <translation>Attach a WAV audio clip</translation>
        </message>
        <message>
            <source>插入空白页失败</source>
            <translation>Failed to insert a blank page</translation>
        </message>
        <message>
            <source>撤销</source>
            <translation>Undo</translation>
        </message>
        <message>
            <source>撤销上一步批注操作 (Ctrl+Z)</source>
            <translation>Undo the last annotation action (Ctrl+Z)</translation>
        </message>
        <message>
            <source>擦掉墨迹（整笔删除）</source>
            <translation>Erase ink (removes the whole stroke)</translation>
        </message>
        <message>
            <source>放大</source>
            <translation>Zoom in</translation>
        </message>
        <message>
            <source>放大 (Ctrl++)</source>
            <translation>Zoom In (Ctrl++)</translation>
        </message>
        <message>
            <source>放映</source>
            <translation>Presentation</translation>
        </message>
        <message>
            <source>整页</source>
            <translation>Fit Page</translation>
        </message>
        <message>
            <source>文字</source>
            <translation>Text</translation>
        </message>
        <message>
            <source>文本文件 (*.txt)</source>
            <translation>Text files (*.txt)</translation>
        </message>
        <message>
            <source>文本框</source>
            <translation>Text box</translation>
        </message>
        <message>
            <source>文档至少要保留一页</source>
            <translation>The document must keep at least one page</translation>
        </message>
        <message>
            <source>新建便签</source>
            <translation>New note</translation>
        </message>
        <message>
            <source>旋转页面失败</source>
            <translation>Failed to rotate the page</translation>
        </message>
        <message>
            <source>未打开文档</source>
            <translation>No document open</translation>
        </message>
        <message>
            <source>椭圆</source>
            <translation>Ellipse</translation>
        </message>
        <message>
            <source>橡皮</source>
            <translation>Eraser</translation>
        </message>
        <message>
            <source>每张纸排几页：</source>
            <translation>Pages per sheet:</translation>
        </message>
        <message>
            <source>水印文字（仅支持西文）:</source>
            <translation>Watermark text (Western text only):</translation>
        </message>
        <message>
            <source>水印没有写进去。</source>
            <translation>The watermark was not written.</translation>
        </message>
        <message>
            <source>水印目前只支持西文：中文需要嵌入字体，暂不支持。</source>
            <translation>Watermarks currently support Western text only; Chinese needs an embedded font and is not supported yet.</translation>
        </message>
        <message>
            <source>没有写入内容。</source>
            <translation>Nothing was written.</translation>
        </message>
        <message>
            <source>没有可撤销的批注操作</source>
            <translation>No annotation action to undo</translation>
        </message>
        <message>
            <source>没有可重做的批注操作</source>
            <translation>No annotation action to redo</translation>
        </message>
        <message>
            <source>没有打开文档</source>
            <translation>No document open</translation>
        </message>
        <message>
            <source>没有目标路径</source>
            <translation>No target path</translation>
        </message>
        <message>
            <source>波浪线</source>
            <translation>Squiggly</translation>
        </message>
        <message>
            <source>测量</source>
            <translation>Measure</translation>
        </message>
        <message>
            <source>添加水印</source>
            <translation>Add watermark</translation>
        </message>
        <message>
            <source>添加附件</source>
            <translation>Add attachment</translation>
        </message>
        <message>
            <source>用一张图片做图章</source>
            <translation>Use an image as a stamp</translation>
        </message>
        <message>
            <source>直线</source>
            <translation>Line</translation>
        </message>
        <message>
            <source>矩形</source>
            <translation>Rectangle</translation>
        </message>
        <message>
            <source>移动页面失败</source>
            <translation>Failed to move the page</translation>
        </message>
        <message>
            <source>第 %1 / %2 页</source>
            <translation>Page %1 of %2</translation>
        </message>
        <message>
            <source>第 %1 页已上移</source>
            <translation>Moved page %1 up</translation>
        </message>
        <message>
            <source>第 %1 页已下移</source>
            <translation>Moved page %1 down</translation>
        </message>
        <message>
            <source>第 %1 页已逆时针旋转 90 度</source>
            <translation>Rotated page %1 90° counterclockwise</translation>
        </message>
        <message>
            <source>第 %1 页已顺时针旋转 90 度</source>
            <translation>Rotated page %1 90° clockwise</translation>
        </message>
        <message>
            <source>箭头</source>
            <translation>Arrow</translation>
        </message>
        <message>
            <source>给选中的文字加下划线 (Ctrl+U)</source>
            <translation>Underline the selected text (Ctrl+U)</translation>
        </message>
        <message>
            <source>给选中的文字加删除线</source>
            <translation>Strike through the selected text</translation>
        </message>
        <message>
            <source>给选中的文字加波浪线</source>
            <translation>Add a squiggly underline to the selected text</translation>
        </message>
        <message>
            <source>给选中的文字加高亮 (Ctrl+H)</source>
            <translation>Highlight the selected text (Ctrl+H)</translation>
        </message>
        <message>
            <source>缓存 %1 MiB · 待渲染 %2</source>
            <translation>Cache %1 MiB · %2 pending</translation>
        </message>
        <message>
            <source>缩小</source>
            <translation>Zoom out</translation>
        </message>
        <message>
            <source>缩小 (Ctrl+-)</source>
            <translation>Zoom Out (Ctrl+-)</translation>
        </message>
        <message>
            <source>缩放到整页可见</source>
            <translation>Zoom to fit the whole page</translation>
        </message>
        <message>
            <source>缩放到适应窗口宽度</source>
            <translation>Zoom to fit the window width</translation>
        </message>
        <message>
            <source>请先选中要标记的文字</source>
            <translation>Select the text to mark first</translation>
        </message>
        <message>
            <source>读不了这个文件: %1</source>
            <translation>Cannot read this file: %1</translation>
        </message>
        <message>
            <source>输入文字（中文会由本程序绘制，其他阅读器按字体回退显示）:</source>
            <translation>Enter text (this app draws Chinese itself; other readers fall back to font glyphs):</translation>
        </message>
        <message>
            <source>这个文档还没有保存过，请用「另存为」</source>
            <translation>This document has not been saved yet; use "Save as"</translation>
        </message>
        <message>
            <source>这看起来不是一张图片。</source>
            <translation>This does not look like an image.</translation>
        </message>
        <message>
            <source>适宽</source>
            <translation>Fit Width</translation>
        </message>
        <message>
            <source>选择图章图片</source>
            <translation>Choose a stamp image</translation>
        </message>
        <message>
            <source>选择图章文字:</source>
            <translation>Choose the stamp text:</translation>
        </message>
        <message>
            <source>选择音频</source>
            <translation>Choose audio</translation>
        </message>
        <message>
            <source>逐点点击，双击结束</source>
            <translation>Click point by point, double-click to finish</translation>
        </message>
        <message>
            <source>逐点点击，双击结束，量周长</source>
            <translation>Click point by point, double-click to finish, and measure the perimeter</translation>
        </message>
        <message>
            <source>逐点点击，双击结束，量面积</source>
            <translation>Click point by point, double-click to finish, and measure the area</translation>
        </message>
        <message>
            <source>重做</source>
            <translation>Redo</translation>
        </message>
        <message>
            <source>重做上一步批注操作 (Ctrl+Shift+Z)</source>
            <translation>Redo the last annotation action (Ctrl+Shift+Z)</translation>
        </message>
        <message>
            <source>附件</source>
            <translation>Attachment</translation>
        </message>
        <message>
            <source>面积</source>
            <translation>Area</translation>
        </message>
        <message>
            <source>页眉文字（留空不加）:</source>
            <translation>Header text (leave empty to skip):</translation>
        </message>
        <message>
            <source>页眉页脚</source>
            <translation>Header and footer</translation>
        </message>
        <message>
            <source>页眉页脚目前只支持西文：中文需要嵌入字体，暂不支持。</source>
            <translation>Headers and footers currently support Western text only; Chinese needs an embedded font and is not supported yet.</translation>
        </message>
        <message>
            <source>页码</source>
            <translation>Page numbers</translation>
        </message>
        <message>
            <source>页码没有写进去。</source>
            <translation>Page numbers were not written.</translation>
        </message>
        <message>
            <source>页脚文字（留空不加）:</source>
            <translation>Footer text (leave empty to skip):</translation>
        </message>
        <message>
            <source>高亮</source>
            <translation>Highlight</translation>
        </message>
        <message><source>清理隐藏信息</source><translation>Sanitize hidden data</translation></message>
        <message><source>本次构建没有启用 qpdf，无法可靠清理隐藏信息。</source><translation>This build does not include qpdf, so hidden data cannot be sanitized reliably.</translation></message>
        <message><source>将删除文档属性、XMP、文档脚本、自动动作，以及嵌入文件（含附件本体）。页面正文、可见批注和表单值会保留。继续吗？</source><translation>This removes document properties, XMP, document scripts, automatic actions, and embedded files (including the file bodies). Page content, visible annotations, and form values are preserved. Continue?</translation></message>
        <message><source>清理结果校验失败：%1</source><translation>Sanitized result validation failed: %1</translation></message>
        <message><source>隐藏信息与嵌入文件已清理；请保存文档以写入磁盘</source><translation>Hidden data and embedded files were removed. Save the document to write the changes to disk.</translation></message>
        <message><source>密文遮盖</source><translation>Redaction</translation></message>
        <message><source>请先选中要永久遮盖的文字区域。</source><translation>Select the text area to redact permanently first.</translation></message>
        <message><source>为避免旋转坐标误判，请先把该页旋转回 0° 再执行遮盖。</source><translation>To avoid incorrect rotated coordinates, rotate the page back to 0° before redacting.</translation></message>
        <message><source>永久密文遮盖</source><translation>Permanent redaction</translation></message>
        <message><source>遮盖会永久删除该页原始内容，并把整页转换为 144 DPI 位图。该页将失去文本搜索和内容编辑能力，操作不可在保存后恢复。继续吗？</source><translation>Redaction permanently removes the original page content and converts the whole page to a 144 DPI bitmap. Text search and content editing will no longer work on this page, and the operation cannot be recovered after saving. Continue?</translation></message>
        <message><source>无法安全地应用遮盖，文档没有修改。</source><translation>The redaction could not be applied safely. The document was not modified.</translation></message>
        <message><source>密文遮盖已烧入第 %1 页；请保存文档</source><translation>Redaction was burned into page %1. Save the document.</translation></message>
        <message><source>文档权限限制</source><translation>Document permission restriction</translation></message>
        <message><source>编辑页面对象</source><translation>Edit page objects</translation></message>
        <message><source>框选页面对象并移动、缩放、删除或修改文字属性</source><translation>Box-select page objects to move, scale, delete, or edit text properties</translation></message>
        <message><source>编辑对象</source><translation>Edit objects</translation></message>
        <message><source>先框选或选中页面对象。</source><translation>Select or box-select page objects first.</translation></message>
        <message><source>请在页面上拖框选择一个或多个对象</source><translation>Drag a box on the page to select one or more objects</translation></message>
        <message><source>选区内没有可编辑的页面对象。</source><translation>There are no editable page objects in the selection.</translation></message>
        <message><source>移动</source><translation>Move</translation></message>
        <message><source>缩放</source><translation>Scale</translation></message>
        <message><source>删除</source><translation>Delete</translation></message>
        <message><source>文字属性</source><translation>Text properties</translation></message>
        <message><source>已选择 %1 个对象：</source><translation>%1 objects selected:</translation></message>
        <message><source>移动对象</source><translation>Move objects</translation></message>
        <message><source>水平移动（点）:</source><translation>Horizontal movement (points):</translation></message>
        <message><source>垂直移动（点）:</source><translation>Vertical movement (points):</translation></message>
        <message><source>缩放对象</source><translation>Scale objects</translation></message>
        <message><source>缩放比例（%）:</source><translation>Scale (%):</translation></message>
        <message><source>水平缩放（%）:</source><translation>Horizontal scale (%):</translation></message>
        <message><source>垂直缩放（%）:</source><translation>Vertical scale (%):</translation></message>
        <message><source>删除对象</source><translation>Delete objects</translation></message>
        <message><source>永久删除选中的 %1 个页面对象？</source><translation>Permanently delete the selected %1 page objects?</translation></message>
        <message><source>选区内没有文字对象。</source><translation>There are no text objects in the selection.</translation></message>
        <message><source>字号:</source><translation>Font size:</translation></message>
        <message><source>字体:</source><translation>Font:</translation></message>
        <message><source>对齐方式:</source><translation>Alignment:</translation></message>
        <message><source>左对齐</source><translation>Align left</translation></message>
        <message><source>居中</source><translation>Center</translation></message>
        <message><source>右对齐</source><translation>Align right</translation></message>
        <message><source>两端对齐</source><translation>Justify</translation></message>
        <message><source>文字颜色</source><translation>Text color</translation></message>
        <message><source>页面对象没有修改。</source><translation>No page objects were modified.</translation></message>
        <message><source>已修改 %1 个页面对象</source><translation>Modified %1 page objects</translation></message>
        <message><source>流式编辑</source><translation>Reflow editing</translation></message>
        <message><source>先选中要重排的文字。</source><translation>Select the text to reflow first.</translation></message>
        <message><source>当前文本框无法容纳任何文字，请增大文本框或减小字号。</source><translation>The text frame cannot hold any text. Enlarge it or reduce the font size.</translation></message>
        <message><source>链接文本框空间不足，仍有 %1 个字符未排入；原内容未修改。</source><translation>The linked frames are full with %1 characters left; the original content was not changed.</translation></message>
        <message><source>无法安全移除原文字对象。</source><translation>The original text objects could not be removed safely.</translation></message>
        <message><source>无法创建页面安全备份，文档没有修改。</source><translation>Could not create a safe page backup. The document was not modified.</translation></message>
        <message><source>部分文字无法用当前字体写入，已恢复原页面。</source><translation>Some text could not be written with the current font. The original page was restored.</translation></message>
        <message><source>已完成段落重排</source><translation>Paragraph reflow completed</translation></message>
        <message><source>已完成行内自动换行</source><translation>Inline wrapping completed</translation></message>
        <message><source>文档所有者已禁止“%1”。是否输入所有者密码，为本次会话解除限制？</source><translation>The document owner has prohibited “%1”. Enter the owner password to unlock it for this session?</translation></message>
        <message><source>解除权限限制</source><translation>Unlock permissions</translation></message>
        <message><source>请先保存当前修改，再输入所有者密码解除权限限制。</source><translation>Save the current changes before entering the owner password to unlock permissions.</translation></message>
        <message><source>所有者密码</source><translation>Owner password</translation></message>
        <message><source>输入 PDF 所有者密码：</source><translation>Enter the PDF owner password:</translation></message>
        <message><source>无法读取原始 PDF。</source><translation>Could not read the original PDF.</translation></message>
        <message><source>所有者密码无效</source><translation>Invalid owner password</translation></message>
        <message><source>已使用所有者密码解除本次会话的权限限制</source><translation>Permissions unlocked for this session with the owner password</translation></message>
        <message><source>文档所有者已禁止“%1”。请输入所有者密码解除限制，或联系文档所有者。</source><translation>The document owner has prohibited “%1”. Enter the owner password to remove the restriction, or contact the document owner.</translation></message>
        <message><source>操作被文档权限阻止：%1</source><translation>Operation blocked by document permissions: %1</translation></message>
        <message><source>复制内容</source><translation>Copy content</translation></message>
        <message><source>填写表单</source><translation>Fill forms</translation></message>
        <message><source>扁平化表单</source><translation>Flatten forms</translation></message>
        <message><source>合并页面</source><translation>Merge pages</translation></message>
        <message><source>抽取页面</source><translation>Extract pages</translation></message>
        <message><source>添加批注</source><translation>Add annotation</translation></message>
        <message><source>删除页面</source><translation>Delete page</translation></message>
        <message><source>旋转页面</source><translation>Rotate page</translation></message>
        <message><source>移动页面</source><translation>Move page</translation></message>
        <message><source>导出页面图片</source><translation>Export page images</translation></message>
            <message>
            <source>表格检测</source>
            <translation>Table detection</translation>
        </message>
        <message>
            <source>按书签拆分</source>
            <translation>Split by bookmarks</translation>
        </message>
        <message>
            <source>按体积拆分</source>
            <translation>Split by size</translation>
        </message>
        <message>
            <source>书签少于两个（或者都指向同一页），没法按书签拆。</source>
            <translation>Fewer than two bookmarks (or they all point to the same page), so it cannot be split by bookmark.</translation>
        </message>
        <message>
            <source>已按书签拆分出 %1 个文件</source>
            <translation>Split into %1 files by bookmark</translation>
        </message>
        <message>
            <source>已按体积拆分出 %1 个文件</source>
            <translation>Split into %1 files by size</translation>
        </message>
        <message>
            <source>每个文件最多多少兆:</source>
            <translation>Maximum size per file (MB):</translation>
        </message>
        <message>
            <source>把当前页移到第几页:</source>
            <translation>Move the current page to position:</translation>
        </message>
        <message>
            <source>第 %1 页已移到第 %2 位</source>
            <translation>Page %1 moved to position %2</translation>
        </message>
        <message>
            <source>这一段没有页。</source>
            <translation>This range contains no pages.</translation>
        </message>
        <message>
            <source>页码格式（%1 是当前页，%2 是总页数）:</source>
            <translation>Page number format (%1 is the current page, %2 is the total):</translation>
        </message>
        <message>
            <source>修改表格单元格</source>
            <translation>Edit table cell</translation>
        </message>
        <message>
            <source>修改单元格</source>
            <translation>Edit cell</translation>
        </message>
        <message>
            <source>修改选中格</source>
            <translation>Edit selected cell</translation>
        </message>
        <message>
            <source>先选中一个单元格。</source>
            <translation>Select a cell first.</translation>
        </message>
        <message>
            <source>第 %1 行第 %2 列的新文字:</source>
            <translation>New text for row %1, column %2:</translation>
        </message>
        <message>
            <source>已修改表格第 %1 行第 %2 列</source>
            <translation>Updated row %1, column %2 of the table</translation>
        </message>
        <message>
            <source>这一格改不了（可能不是单个文本对象，或者已经变了）。</source>
            <translation>This cell cannot be edited (it may not be a single text object, or it has changed).</translation>
        </message>
        <message>
            <source>检测到 %1 行 × %2 列。选中一格后可以改写它的文字。</source>
            <translation>Detected %1 rows × %2 columns. Select a cell to rewrite its text.</translation>
        </message>
        <message>
            <source>这里没有检测到表格结构（至少要两行两列）。</source>
            <translation>No table structure was detected here (at least two rows and two columns are required).</translation>
        </message>
        <message>
            <source>写入 %1 失败。</source>
            <translation>Failed to write %1.</translation>
        </message>
        <message>
            <source>第 %1 页写入失败。</source>
            <translation>Failed to write page %1.</translation>
        </message>
        <message>
            <source>第 %1 页备份失败。</source>
            <translation>Failed to back up page %1.</translation>
        </message>
        <message>
            <source>写入后校验失败：%1</source>
            <translation>Verification failed after writing: %1</translation>
        </message>
        <message>
            <source>写入表单脚本</source>
            <translation>Write form scripts</translation>
        </message>
        <message>
            <source>这份文档不允许修改。</source>
            <translation>This document does not allow modifications.</translation>
        </message>
        <message>
            <source>单选分组</source>
            <translation>Radio group</translation>
        </message>
        <message>
            <source>同名的单选按钮归为一组</source>
            <translation>Radio buttons with the same group name form one group</translation>
        </message>
        <message>
            <source>条码内容</source>
            <translation>Barcode content</translation>
        </message>
        <message>
            <source>条码内容（只能是 ASCII）</source>
            <translation>Barcode content (ASCII only)</translation>
        </message>
        <message>
            <source>条码说明</source>
            <translation>Barcode caption</translation>
        </message>
        <message>
            <source>条码下方的说明文字</source>
            <translation>Caption printed below the barcode</translation>
        </message>
        <message>
            <source>引用哪个书签:</source>
            <translation>Which bookmark to reference:</translation>
        </message>
        <message>
            <source>引用文字:</source>
            <translation>Reference text:</translation>
        </message>
        <message>
            <source>文档里还没有书签。先在「编辑 &gt; 导航 &gt; 书签」里建书签，再来做交叉引用。</source>
            <translation>This document has no bookmarks yet. Create one under Edit &gt; Navigation &gt; Bookmarks first.</translation>
        </message>
        <message>
            <source>这个书签没有有效的目标页。</source>
            <translation>This bookmark has no valid target page.</translation>
        </message>
        <message>
            <source>已插入交叉引用，跳转到第 %1 页</source>
            <translation>Cross-reference inserted; jumps to page %1</translation>
        </message>
        <message>
            <source>框里的文字:</source>
            <translation>Text in the frame:</translation>
        </message>
        <message>
            <source>字号（点）:</source>
            <translation>Font size (points):</translation>
        </message>
        <message>
            <source>这一页放不下这个框。</source>
            <translation>This frame does not fit on the page.</translation>
        </message>
        <message>
            <source>添加失败。</source>
            <translation>Failed to add.</translation>
        </message>
        <message>
            <source>已添加文章框</source>
            <translation>Article frame added</translation>
        </message>
        <message>
            <source>交叉引用</source>
            <translation>Cross-reference</translation>
        </message>
        <message>
            <source>添加文章框</source>
            <translation>Article frame</translation>
        </message>
        <message>
            <source>字数统计</source>
            <translation>Word count</translation>
        </message>
        <message>
            <source>总字数：%1
字符数：%2（不含空白 %3）
页数：%4
识别到的标题：%5</source>
            <translation>Total words: %1
Characters: %2 (excluding spaces %3)
Pages: %4
Headings found: %5</translation>
        </message>
        <message>
            <source>页</source>
            <translation>Page</translation>
        </message>
        <message>
            <source>字数</source>
            <translation>Words</translation>
        </message>
        <message>
            <source>字符数</source>
            <translation>Characters</translation>
        </message>
        <message>
            <source>文档属性</source>
            <translation>Document properties</translation>
        </message>
        <message>
            <source>标题:</source>
            <translation>Title:</translation>
        </message>
        <message>
            <source>作者:</source>
            <translation>Author:</translation>
        </message>
        <message>
            <source>主题:</source>
            <translation>Subject:</translation>
        </message>
        <message>
            <source>关键字:</source>
            <translation>Keywords:</translation>
        </message>
        <message>
            <source>创建程序:</source>
            <translation>Creator:</translation>
        </message>
        <message>
            <source>生成程序:</source>
            <translation>Producer:</translation>
        </message>
        <message>
            <source>创建时间:</source>
            <translation>Created:</translation>
        </message>
        <message>
            <source>修改时间:</source>
            <translation>Modified:</translation>
        </message>
        <message>
            <source>已保存文档属性</source>
            <translation>Document properties saved</translation>
        </message>
        <message>
            <source>自动创建书签</source>
            <translation>Auto-create bookmarks</translation>
        </message>
        <message>
            <source>没有识别到标题：这份文档既没有可用的标题元素，也没有字高比正文大的行。</source>
            <translation>No headings were found: this document has neither usable heading elements nor lines that are larger than the body text.</translation>
        </message>
        <message>
            <source>已生成 %1 条书签（读自文档的标签树）。</source>
            <translation>Generated %1 bookmarks from the document's tag tree.</translation>
        </message>
        <message>
            <source>…还有 %1 条</source>
            <translation>... and %1 more</translation>
        </message>
        <message>
            <source>%1

%2

确定要写入文档吗？</source>
            <translation>%1

%2

Write them into the document?</translation>
        </message>
        <message>
            <source>已生成 %1 条书签</source>
            <translation>Generated %1 bookmarks</translation>
        </message>
        <message>
            <source>以下内容按页面上的视觉顺序排列（多栏与表格已按位置重排），可用于人工核对屏幕阅读器的阅读次序。</source>
            <translation>The text below is in visual order on the page (multi-column and table layouts are reordered by position); use it to check a screen reader's reading order.</translation>
        </message>
        <message>
            <source>%1 已完成</source>
            <translation>%1 completed</translation>
        </message>
        <message>
            <source>「%1」：%2</source>
            <translation>"%1": %2</translation>
        </message>
        <message>
            <source>为避免旋转坐标误判，请先把该页旋转回 0° 再执行。</source>
            <translation>To avoid misjudging rotated coordinates, rotate the page back to 0° first.</translation>
        </message>
        <message>
            <source>以下规则没有生效：
%1</source>
            <translation>The following rules did not take effect:
%1</translation>
        </message>
        <message>
            <source>删除选中规则</source>
            <translation>Delete selected rules</translation>
        </message>
        <message>
            <source>可疑的词</source>
            <translation>Suspicious word</translation>
        </message>
        <message>
            <source>处理失败，文档没有修改。</source>
            <translation>Processing failed; the document was not modified.</translation>
        </message>
        <message>
            <source>处理方式:</source>
            <translation>Processing method:</translation>
        </message>
        <message>
            <source>密文处理</source>
            <translation>Redaction</translation>
        </message>
        <message>
            <source>应用密文</source>
            <translation>Apply redaction</translation>
        </message>
        <message>
            <source>应用密文（整页位图化，文字层全部消失，页面失去搜索与编辑能力）</source>
            <translation>Apply redaction (the whole page becomes a bitmap, the text layer disappears and the page loses search and editing)</translation>
        </message>
        <message>
            <source>建议</source>
            <translation>Suggestions</translation>
        </message>
        <message>
            <source>开始替换</source>
            <translation>Start replacing</translation>
        </message>
        <message>
            <source>开始朗读</source>
            <translation>Starting to read aloud</translation>
        </message>
        <message>
            <source>批量替换</source>
            <translation>Batch replace</translation>
        </message>
        <message>
            <source>批量替换完成：共改了 %1 处</source>
            <translation>Batch replace finished: %1 replacements in total</translation>
        </message>
        <message>
            <source>拼写检查</source>
            <translation>Spell check</translation>
        </message>
        <message>
            <source>擦除</source>
            <translation>Erase</translation>
        </message>
        <message>
            <source>擦除（删掉该区域的对象，页面其余部分仍是矢量文字）</source>
            <translation>Erase (objects in the area are removed; the rest of the page stays vector text)</translation>
        </message>
        <message>
            <source>替换为</source>
            <translation>Replace with</translation>
        </message>
        <message>
            <source>替换失败，文档没有修改。</source>
            <translation>Replacement failed; the document was not modified.</translation>
        </message>
        <message>
            <source>替换结果无法重新打开，文档没有修改：%1</source>
            <translation>The replacement result cannot be reopened; the document was not modified: %1</translation>
        </message>
        <message>
            <source>朗读</source>
            <translation>Read aloud</translation>
        </message>
        <message>
            <source>朗读失败：%1</source>
            <translation>Reading failed: %1</translation>
        </message>
        <message>
            <source>朗读结束</source>
            <translation>Finished reading aloud</translation>
        </message>
        <message>
            <source>查找</source>
            <translation>Find</translation>
        </message>
        <message>
            <source>标记为密文</source>
            <translation>Mark as redacted</translation>
        </message>
        <message>
            <source>标记为密文（只盖黑块，原文仍在文件里，文字层照样能搜到）</source>
            <translation>Mark as redacted (draws a black box only; the original text is still in the file and can still be found)</translation>
        </message>
        <message>
            <source>检查范围:</source>
            <translation>Check range:</translation>
        </message>
        <message>
            <source>正在提取文本，请稍候</source>
            <translation>Extracting text, please wait</translation>
        </message>
        <message>
            <source>正在朗读：%1</source>
            <translation>Reading: %1</translation>
        </message>
        <message>
            <source>没有可执行的替换规则。</source>
            <translation>There are no replacement rules to run.</translation>
        </message>
        <message>
            <source>添加规则</source>
            <translation>Add rule</translation>
        </message>
        <message>
            <source>系统没有可用的语音：%1</source>
            <translation>No voice is available on this system: %1</translation>
        </message>
        <message>
            <source>请先选中要处理的文字区域。</source>
            <translation>Select the text area to process first.</translation>
        </message>
        <message>
            <source>这一页没有可朗读的文字。</source>
            <translation>There is no readable text on this page.</translation>
        </message>
        <message>
            <source>选区无效，文档没有修改。</source>
            <translation>The selection is invalid; the document was not modified.</translation>
        </message>
        <message>
            <source>马赛克</source>
            <translation>Mosaic</translation>
        </message>
        <message>
            <source>马赛克（把区域像素化，文字层里读不到该区域）</source>
            <translation>Mosaic (pixelates the area; the text layer can no longer read it)</translation>
        </message>
        <message>
            <source>全字匹配</source>
            <translation>Whole word match</translation>
        </message>
        <message>
            <source>区分大小写</source>
            <translation>Case sensitive</translation>
        </message>
        <message>
            <source>正则</source>
            <translation>Regular expression</translation>
        </message>
        <message>
            <source>签名列表</source>
            <translation>Signature list</translation>
        </message>
        <message>
            <source>签名域</source>
            <translation>Signature field</translation>
        </message>
        <message>
            <source>签署人</source><translation>Signer</translation>
        </message>
        <message>
            <source>签署时间</source>
            <translation>Signing time</translation>
        </message>
        <message>
            <source>算法</source>
            <translation>Algorithm</translation>
        </message>
        <message>
            <source>覆盖检查</source>
            <translation>Coverage check</translation>
        </message>
        <message>
            <source>证书链</source>
            <translation>Certificate chain</translation>
        </message>
        <message>
            <source>主体</source>
            <translation>Subject</translation>
        </message>
        <message>
            <source>签发者</source>
            <translation>Issuer</translation>
        </message>
        <message>
            <source>序列号</source>
            <translation>Serial number</translation>
        </message>
        <message>
            <source>SHA-1 指纹</source>
            <translation>SHA-1 thumbprint</translation>
        </message>
        <message>
            <source>生效</source>
            <translation>Valid from</translation>
        </message>
        <message>
            <source>失效</source>
            <translation>Valid to</translation>
        </message>
        <message>
            <source>状态</source>
            <translation>Status</translation>
        </message>
        <message>
            <source>受信任</source>
            <translation>Trusted</translation>
        </message>
        <message>
            <source>自签（不受系统信任）</source>
            <translation>Self-signed (not trusted by the system)</translation>
        </message>
        <message>
            <source>不受系统信任</source>
            <translation>Not trusted by the system</translation>
        </message>
        <message>
            <source>覆盖完整</source>
            <translation>Coverage is complete</translation>
        </message>
        <message>
            <source>覆盖不完整</source>
            <translation>Coverage is incomplete</translation>
        </message>
        <message>
            <source>签名域：%1</source>
            <translation>Signature field: %1</translation>
        </message>
        <message>
            <source>签署人：%1</source>
            <translation>Signer: %1</translation>
        </message>
        <message>
            <source>签署时间：%1</source>
            <translation>Signing time: %1</translation>
        </message>
        <message>
            <source>签名理由：%1</source>
            <translation>Signing reason: %1</translation>
        </message>
        <message>
            <source>算法：%1</source>
            <translation>Algorithm: %1</translation>
        </message>
        <message>
            <source>覆盖区间：</source>
            <translation>Covered ranges:</translation>
        </message>
        <message>
            <source>从第 %1 字节起共 %2 字节</source>
            <translation>%2 bytes starting at byte %1</translation>
        </message>
        <message>
            <source>这个版本没有启用签名解析（构建时没有 qpdf）。</source>
            <translation>This build does not include signature inspection (qpdf was not enabled at build time).</translation>
        </message>
        <message>
            <source>这份文档里没有数字签名。</source>
            <translation>This document has no digital signature.</translation>
        </message>
        <message>
            <source>覆盖检查：%1</source>
            <translation>Coverage check: %1</translation>
        </message>
        <message>
            <source>（没有读到覆盖区间）</source>
            <translation>(no covered range was read)</translation>
        </message>
        <message>
            <source>区域阅读顺序</source>
            <translation>Region reading order</translation>
        </message>
        <message>
            <source>更改区域顺序</source>
            <translation>Change region order</translation>
        </message>
        <message>
            <source>应用区域顺序</source>
            <translation>Apply region order</translation>
        </message>
        <message>
            <source>写入</source>
            <translation>Write</translation>
        </message>
        <message>
            <source>上移</source>
            <translation>Move up</translation>
        </message>
        <message>
            <source>下移</source>
            <translation>Move down</translation>
        </message>
        <message>
            <source>这一页没有可标记的文字。</source>
            <translation>There is no markable text on this page.</translation>
        </message>
        <message>
            <source>写入失败，文档没有修改。</source>
            <translation>Writing failed; the document was not modified.</translation>
        </message>
        <message>
            <source>写入标签树失败，文档没有修改。</source>
            <translation>Writing the tag tree failed; the document was not modified.</translation>
        </message>
        <message>
            <source>写入后的文档无法重新打开，文档没有修改：%1</source>
            <translation>The document cannot be reopened after writing; the document was not modified: %1</translation>
        </message>
        <message>
            <source>这份文档里还没有标签树。用「更改区域顺序」或「应用区域顺序」写一份。</source>
            <translation>This document has no tag tree yet. Use "Change region order" or "Apply region order" to write one.</translation>
        </message>
        <message>
            <source>第 %1 页：%2 个区域，顺序 %3</source>
            <translation>Page %1: %2 regions, order %3</translation>
        </message>
        <message>
            <source>下面是当前页按视觉顺序排出的行。调整先后后写进 PDF 的标签树，读屏软件会按这个顺序念。</source>
            <translation>Below are the lines of the current page in visual order. Adjusting the order writes them into the PDF tag tree, and a screen reader will read them in that order.</translation>
        </message>
        <message>
            <source>已把第 %1 页的阅读顺序写进标签树</source>
            <translation>Wrote the reading order of page %1 into the tag tree</translation>
        </message>
        <message>
            <source>已按默认顺序写好标签树</source>
            <translation>Wrote the tag tree in the default order</translation>
        </message>
        <message>
            <source>会按「从上到下、从左到右」的顺序给整篇文档写一份标签树。页面外观不变，但读屏软件会按这个顺序念。继续吗？</source>
            <translation>A tag tree will be written for the whole document in "top to bottom, left to right" order. The pages look the same, but a screen reader will read them in that order. Continue?</translation>
        </message>
    <message><source>页码标签</source><translation>Page labels</translation></message>
    <message><source>阿拉伯数字 (1, 2, 3)</source><translation>Arabic numerals (1, 2, 3)</translation></message>
    <message><source>大写罗马 (I, II, III)</source><translation>Uppercase Roman numerals (I, II, III)</translation></message>
    <message><source>小写罗马 (i, ii, iii)</source><translation>Lowercase Roman numerals (i, ii, iii)</translation></message>
    <message><source>大写字母 (A, B, C)</source><translation>Uppercase letters (A, B, C)</translation></message>
    <message><source>小写字母 (a, b, c)</source><translation>Lowercase letters (a, b, c)</translation></message>
    <message><source>前缀（例如 A-）</source><translation>Prefix (for example A-)</translation></message>
    <message><source>已写入页码标签</source><translation>Page labels written</translation></message>
    <message><source>命名位置</source><translation>Named destinations</translation></message>
    <message><source>给当前页起一个名字，之后可以按名字跳回来。</source><translation>Give the current page a name so you can jump back to it later.</translation></message>
    <message><source>名称（例如 封面）</source><translation>Name (for example Cover)</translation></message>
    <message><source>跳转到所选</source><translation>Jump to selected</translation></message>
    <message><source>已写入命名位置：%1</source><translation>Named destination written: %1</translation></message>
    <message><source>批处理</source><translation>Batch processing</translation></message>
    <message><source>选一个目录，把里面的 PDF 依次处理一遍，结束时给一份 JSON 报告。</source><translation>Pick a folder, process every PDF in it one by one, and get a JSON report at the end.</translation></message>
    <message><source>选择目录…</source><translation>Choose folder…</translation></message>
    <message><source>选择要批处理的目录</source><translation>Choose the folder to process</translation></message>
    <message><source>输出目录（留空则只跑处理、不落盘）</source><translation>Output folder (leave empty to process without writing files)</translation></message>
    <message><source>移除嵌入文件</source><translation>Remove embedded files</translation></message>
    <message><source>什么也不做（只出报告）</source><translation>Do nothing (report only)</translation></message>
    <message><source>先选一个目录。</source><translation>Choose a folder first.</translation></message>
    <message><source>这个目录里没有 PDF。</source><translation>There are no PDFs in this folder.</translation></message>
    <message><source>至少勾一个处理步骤。</source><translation>Check at least one processing step.</translation></message>
    <message><source>开始</source><translation>Start</translation></message>
    <message><source>批处理完成：成功 %1，失败 %2</source><translation>Batch finished: %1 succeeded, %2 failed</translation></message>
    <message><source>已合并 %1 份文件：共 %2 页</source><translation>Merged %1 files: %2 pages total</translation></message>
    <message><source>打不开 %1：%2</source><translation>Cannot open %1: %2</translation></message>
    <message><source>读不出 %1：%2</source><translation>Cannot read %1: %2</translation></message>
    <message><source>合并结果校验失败：%1</source><translation>The merged result failed validation: %1</translation></message>
    <message><source>箭头框</source><translation>Callout</translation></message>
    <message><source>箭头框生成失败。</source><translation>Failed to generate the callout.</translation></message>
    <message><source>箭头框没有添加成功</source><translation>The callout could not be added</translation></message>
    <message><source>已添加箭头框</source><translation>Callout added</translation></message>
    <message><source>区域高亮</source><translation>Region highlight</translation></message>
    <message><source>区域高亮至少需要 3 个顶点</source><translation>A region highlight needs at least 3 points</translation></message>
    <message><source>这个区域自交了（轮廓自己交叉），画不出高亮，请重新圈一次。</source><translation>This region is self-intersecting, so it cannot be highlighted. Please draw it again.</translation></message>
    <message><source>区域高亮生成失败。</source><translation>Failed to generate the region highlight.</translation></message>
    <message><source>区域高亮没有添加成功</source><translation>The region highlight could not be added</translation></message>
    <message><source>已添加区域高亮</source><translation>Region highlight added</translation></message>
    <message><source>纯文本查看器</source><translation>Plain text viewer</translation></message>
    <message><source>纯文本查看器 - 第 %1 页</source><translation>Plain text viewer - page %1</translation></message>
    <message><source>纯文本查看器 - 整篇</source><translation>Plain text viewer - whole document</translation></message>
    <message><source>这一页没有可提取的文字。</source><translation>This page has no extractable text.</translation></message>
    <message><source>这份文档没有可提取的文字。</source><translation>This document has no extractable text.</translation></message>
    <message><source>密码学判定</source><translation>Cryptographic verdict</translation></message>
    <message><source>密码学判定：%1</source><translation>Cryptographic verdict: %1</translation></message>
    <message><source>签名有效</source><translation>Signature valid</translation></message>
    <message><source>签名无效</source><translation>Signature invalid</translation></message>
    <message><source>无法判定</source><translation>Cannot be determined</translation></message>
    <message><source>摘要算法：%1</source><translation>Digest algorithm: %1</translation></message>
    <message><source>内容摘要一致：%1</source><translation>Content digest matches: %1</translation></message>
    <message><source>签名者：%1</source><translation>Signer: %1</translation></message>
    <message><source>签名者主体：%1</source><translation>Signer subject: %1</translation></message>
    <message><source>时间戳：%1</source><translation>Timestamp: %1</translation></message>
    <message><source>时间戳机构：%1</source><translation>Timestamp authority: %1</translation></message>
    <message><source>是</source><translation>Yes</translation></message>
    <message><source>否</source><translation>No</translation></message>
    <message><source>签署流程</source><translation>Signing workflow</translation></message>
    <message><source>标题</source><translation>Title</translation></message>
    <message><source>发起人</source><translation>Initiator</translation></message>
    <message><source>草稿</source><translation>Draft</translation></message>
    <message><source>进行中</source><translation>In progress</translation></message>
    <message><source>已完成</source><translation>Completed</translation></message>
    <message><source>已中止</source><translation>Aborted</translation></message>
    <message><source>（还没有签署人）</source><translation>(No signatories yet)</translation></message>
    <message><source>姓名</source><translation>Name</translation></message>
    <message><source>联系方式</source><translation>Contact</translation></message>
    <message><source>顺序</source><translation>Order</translation></message>
    <message><source>自动</source><translation>Auto</translation></message>
    <message><source>待签署</source><translation>Pending</translation></message>
    <message><source>已签署</source><translation>Signed</translation></message>
    <message><source>已拒签</source><translation>Rejected</translation></message>
    <message><source>已跳过</source><translation>Skipped</translation></message>
    <message><source>添加签署人</source><translation>Add signatory</translation></message>
    <message><source>删除签署人</source><translation>Remove signatory</translation></message>
    <message><source>先选中要删除的签署人。</source><translation>Select the signatory to remove first.</translation></message>
    <message><source>签署人 %1 不能删除：签署记录要保留完整名单。</source><translation>Signatory %1 cannot be removed: the signing record keeps the full list.</translation></message>
    <message><source>开始签署</source><translation>Start signing</translation></message>
    <message><source>标记已签</source><translation>Mark as signed</translation></message>
    <message><source>标记拒签</source><translation>Mark as rejected</translation></message>
    <message><source>跳过</source><translation>Skip</translation></message>
    <message><source>完成</source><translation>Complete</translation></message>
    <message><source>中止</source><translation>Abort</translation></message>
    <message><source>没有待签署的人。</source><translation>Nobody is pending signature.</translation></message>
    <message><source>拒签理由（必填）</source><translation>Reason for rejection (required)</translation></message>
    <message><source>拒签必须写理由。</source><translation>Rejecting requires a reason.</translation></message>
    <message><source>中止理由（必填）</source><translation>Reason for aborting (required)</translation></message>
    <message><source>中止必须写理由。</source><translation>Aborting requires a reason.</translation></message>
    <message><source>操作没有成功。</source><translation>The operation did not succeed.</translation></message>
    <message><source>创建签署流程失败。</source><translation>Failed to create the signing workflow.</translation></message>
    <message><source>签署记录</source><translation>Signing record</translation></message>
    <message><source>（还没有签署记录）</source><translation>(No signing records yet)</translation></message>
    <message><source>时间</source><translation>Time</translation></message>
    <message><source>谁</source><translation>Who</translation></message>
    <message><source>做了什么</source><translation>Action</translation></message>
    <message><source>说明</source><translation>Detail</translation></message>
    <message><source>创建流程</source><translation>Workflow created</translation></message>
    <message><source>签署完成</source><translation>Signed</translation></message>
    <message><source>拒签</source><translation>Rejected</translation></message>
    <message><source>完成流程</source><translation>Workflow completed</translation></message>
    <message><source>中止流程</source><translation>Workflow aborted</translation></message>
    <message><source>签署报告</source><translation>Signing report</translation></message>
    <message><source>查看签署报告</source><translation>View signing report</translation></message>
    <message><source>复制到剪贴板</source><translation>Copy to clipboard</translation></message>
    <message><source>导出为文本文件</source><translation>Export as text file</translation></message>
    <message><source>导出签署报告</source><translation>Export signing report</translation></message>
    <message><source>（未知）</source><translation>(unknown)</translation></message>
    <message><source>签署人名单</source><translation>Signatories</translation></message>
    <message><source>新增签署人</source><translation>Signatory added</translation></message>
    <message><source>开始签署流程</source><translation>Signing started</translation></message>
    <message><source>跳过签署</source><translation>Skipped</translation></message>
    <message><source>已关闭连续批注</source><translation>Continuous annotation off</translation></message>
    <message><source>已开启连续批注：放完批注不切回选择工具</source><translation>Continuous annotation on: the tool stays selected after placing an annotation</translation></message>
    <message><source>已把生成的图片插入当前页</source><translation>The generated image was inserted into the current page</translation></message>
    <message><source>已插入文本批注</source><translation>Text annotation inserted</translation></message>
    <message><source>插入图片失败</source><translation>Could not insert the image</translation></message>
    <message><source>插入文本批注失败</source><translation>Could not insert the text annotation</translation></message>
    <message><source>智能朗读</source><translation>Smart Read-Aloud</translation></message>
    <message><source>智能朗读结束</source><translation>Smart read-aloud finished</translation></message>
    <message><source>智能朗读：%1 段，预计 %2 秒</source><translation>Smart read-aloud: %1 segment(s), about %2 seconds</translation></message>
    <message><source>生成结果不是一张可用的图片</source><translation>The generated result is not a usable image</translation></message>
    <message><source>读取文档内容失败。</source><translation>Could not read the document content.</translation></message>
    <message><source>这一页放不下这个文本框</source><translation>This text box does not fit on this page</translation></message>
    <message><source>先选中一段文字。</source><translation>Select some text first.</translation></message>
    <message><source>加水印失败。</source><translation>Adding the watermark failed.</translation></message>
    <message><source>加页码失败。</source><translation>Adding page numbers failed.</translation></message>
    <message><source>动作向导还没有接这一步：%1。请用对应的功能入口单独做。</source><translation>The action wizard does not handle this step yet: %1. Use the matching feature entry instead.</translation></message>
    <message><source>已按段落参数重排 %1 个文字对象</source><translation>Re-laid out %1 text object(s) with the paragraph parameters</translation></message>
    <message><source>文档标签树</source><translation>Document tag tree</translation></message>
    <message><source>新的内容是空的，文档没有修改。</source><translation>The new content is empty; the document was not changed.</translation></message>
    <message><source>本程序的水印只能放在页面正中（position 只支持 center）。</source><translation>This program can only put a watermark in the centre of the page (position only supports center).</translation></message>
    <message><source>本程序的页码从第 1 页开始（startNumber 只支持 1）。</source><translation>Page numbers in this program start at page 1 (startNumber only supports 1).</translation></message>
    <message><source>本程序的页码只能放在右下角（position 只支持 bottomRight）。</source><translation>This program can only put page numbers at the bottom right (position only supports bottomRight).</translation></message>
    <message><source>段落排版</source><translation>Paragraph formatting</translation></message>
    <message><source>水印这一步没有文字。</source><translation>This watermark step has no text.</translation></message>
    <message><source>没有生成可写入的内容流。</source><translation>No writable content stream was produced.</translation></message>
    <message><source>没能移除原来的文字对象，文档没有修改。</source><translation>The original text objects could not be removed; the document was not changed.</translation></message>
    <message><source>读不到文档内容。</source><translation>The document content could not be read.</translation></message>
    <message><source>读不到文档内容，文档没有修改。</source><translation>The document content could not be read; the document was not changed.</translation></message>
    <message><source>读取文档标签树</source><translation>Read the document tag tree</translation></message>
    <message><source>选区内没有可排版的文字。</source><translation>There is no text to lay out in the selection.</translation></message>
            <message>
                <source>加密（AES-256）</source>
                <translation>Encrypt (AES-256)</translation>
            </message>
            <message>
                <source>打开密码不能为空。</source>
                <translation>The open password cannot be empty.</translation>
            </message>
            <message>
                <source>没有勾选任何类别，文档保持原样。</source>
                <translation>No category was checked, so the document is left unchanged.</translation>
            </message>
            <message>
                <source>已清理 %1 类隐藏数据：%2；请保存文档以写入磁盘</source>
                <translation>Cleared %1 hidden data categor(y/ies): %2; save the document to write it to disk.</translation>
            </message>
        <message>
            <source>朗读设置已保存</source>
            <translation>Narration settings saved</translation>
        </message>
        <message>
            <source>编号图章：在页面上点一下就放一个，编号自动递增</source>
            <translation>Number stamp: click the page to place one, the number increments automatically</translation>
        </message>
        <message>
            <source>阅读选项</source>
            <translation>Reading options</translation>
        </message>
</context>
    <context>
        <name>aipdf::app::PresentationWindow</name>
        <message>
            <source>放映</source>
            <translation>Presentation</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::ProposalRunner</name>
        <message>
            <source>AI 草稿</source>
            <translation>AI draft</translation>
        </message>
        <message>
            <source>提案里没有可执行的操作</source>
            <translation>The proposal contains no executable operations</translation>
        </message>
        <message>
            <source>没有可用的执行入口</source>
            <translation>No execution handler is available</translation>
        </message>
        <message>
            <source>没有对应的编辑命令</source>
            <translation>No matching edit command</translation>
        </message>
        <message>
            <source>第 %1 条「%2」执行失败</source>
            <translation>Operation %1 "%2" failed</translation>
        </message>
        <message>
            <source>第 %1 条操作（%2）无法执行：%3</source>
            <translation>Operation %1 (%2) cannot be executed: %3</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::SearchPanel</name>
        <message>
            <source>上一处</source>
            <translation>Previous match</translation>
        </message>
        <message>
            <source>上一处结果</source>
            <translation>Previous result</translation>
        </message>
        <message>
            <source>下一处</source>
            <translation>Next match</translation>
        </message>
        <message>
            <source>下一处结果</source>
            <translation>Next result</translation>
        </message>
        <message>
            <source>不限</source>
            <translation>Any</translation>
        </message>
        <message>
            <source>停止</source>
            <translation>Stop</translation>
        </message>
        <message>
            <source>停止搜索</source>
            <translation>Stop search</translation>
        </message>
        <message>
            <source>全字匹配</source>
            <translation>Whole word</translation>
        </message>
        <message>
            <source>共 %1 处结果</source>
            <translation>%1 matches</translation>
        </message>
        <message>
            <source>区分大小写</source>
            <translation>Case sensitive</translation>
        </message>
        <message>
            <source>搜索关键词</source>
            <translation>Search keywords</translation>
        </message>
        <message>
            <source>搜索已取消（共 %1 处结果）</source>
            <translation>Search cancelled (%1 matches)</translation>
        </message>
        <message>
            <source>搜索文档…</source>
            <translation>Search the document…</translation>
        </message>
        <message>
            <source>搜索结果</source>
            <translation>Search results</translation>
        </message>
        <message>
            <source>未找到匹配结果</source>
            <translation>No matches found</translation>
        </message>
        <message>
            <source>正则</source>
            <translation>Regex</translation>
        </message>
        <message>
            <source>正在搜索…</source>
            <translation>Searching…</translation>
        </message>
        <message>
            <source>正在搜索…（已找到 %1 处）</source>
            <translation>Searching… (%1 found so far)</translation>
        </message>
        <message>
            <source>清除</source>
            <translation>Clear</translation>
        </message>
        <message>
            <source>清除搜索</source>
            <translation>Clear search</translation>
        </message>
        <message>
            <source>第</source>
            <translation>Page</translation>
        </message>
        <message>
            <source>第 %1 页 · %2</source>
            <translation>Page %1 · %2</translation>
        </message>
        <message>
            <source>至</source>
            <translation>to</translation>
        </message>
        <message>
            <source>输入关键词开始搜索</source>
            <translation>Enter a keyword to start searching</translation>
        </message>
        <message>
            <source>页</source>
            <translation>pages</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::UploadPreviewDialog</name>
        <message>
            <source>、</source>
            <translation>, </translation>
        </message>
        <message>
            <source>内容：%1 块 · %2 字</source>
            <translation>Content: %1 chunks · %2 characters</translation>
        </message>
        <message>
            <source>发送</source>
            <translation>Send</translation>
        </message>
                <message>
            <source>发送之前</source>
            <translation>Before sending</translation>
        </message>
<message>
            <source>发送到云端之前</source>
            <translation>Before Sending to the Cloud</translation>
        </message>
        <message>
            <source>只发送上面列出的内容。点「取消」不会发出任何请求。</source>
            <translation>Only the content listed above is sent. Clicking "Cancel" sends no request.</translation>
        </message>
        <message>
            <source>数据模式：%1</source>
            <translation>Data mode: %1</translation>
        </message>
        <message>
            <source>本地（不发送）</source>
            <translation>Local (not sent)</translation>
        </message>
                <message>
            <source>本地（内容不出本机）</source>
            <translation>Local (content stays on this machine)</translation>
        </message>
<message>
            <source>涉及页：第 %1 页</source>
            <translation>Pages involved: %1</translation>
        </message>
        <message>
            <source>片段云端</source>
            <translation>Chunk cloud</translation>
        </message>
        <message>
            <source>目标服务：%1</source>
            <translation>Target service: %1</translation>
        </message>
        <message>
            <source>这次请求会离开本机</source>
            <translation>This request will leave this device</translation>
        </message>
                <message>
            <source>这次请求的目标在本机</source>
            <translation>This request targets this machine</translation>
        </message>
<message>
            <source>选定页面</source>
            <translation>Selected pages</translation>
        </message>
        <message>
            <source>（未配置）</source>
            <translation>(not configured)</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::WelcomePage</name>
        <message>
            <source>%1 份文档</source>
            <translation>%1 documents</translation>
        </message>
        <message>
            <source>Ctrl+O 打开  ·  Ctrl+F 查找  ·  支持多选与拖放</source>
            <translation>Ctrl+O Open  ·  Ctrl+F Find  ·  Multi-select and drag-and-drop supported</translation>
        </message>
        <message>
            <source>专注阅读，也为文档智能留出恰到好处的空间</source>
            <translation>Focused reading with just the right room for document intelligence</translation>
        </message>
        <message>
            <source>也可以点击下方按钮从本机选择</source>
            <translation>Or choose one from your computer below</translation>
        </message>
        <message>
            <source>打开 PDF</source>
            <translation>Open PDF</translation>
        </message>
        <message>
            <source>打开过的 PDF 会列在这里，方便下次接着读</source>
            <translation>PDFs you open will appear here for next time</translation>
        </message>
        <message>
            <source>把 PDF 拖到这里</source>
            <translation>Drop a PDF here</translation>
        </message>
        <message>
            <source>暂无最近文档</source>
            <translation>No recent documents</translation>
        </message>
        <message>
            <source>最近打开</source>
            <translation>Recently opened</translation>
        </message>
    </context>
            <context>
        <name>aipdf::app::WizardPageRange</name>
        <message>
            <source>没有地方可以写页范围结果。</source>
            <translation>There is nowhere to write the page range result.</translation>
        </message>
        <message>
            <source>这份文档没有页面。</source>
            <translation>This document has no pages.</translation>
        </message>
        <message>
            <source>页范围是空的。</source>
            <translation>The page range is empty.</translation>
        </message>
        <message>
            <source>页范围 %1 里的页码不是数字。</source>
            <translation>The page number in page range %1 is not a number.</translation>
        </message>
        <message>
            <source>页范围 %1 的起止页反了。</source>
            <translation>The start and end pages of page range %1 are reversed.</translation>
        </message>
        <message>
            <source>页范围 %1 里没有能解析的页码。</source>
            <translation>No page number could be parsed in page range %1.</translation>
        </message>
    </context>
<context>
        <name>aipdf::doc::BrowserPdfPrinter</name>
        <message>
            <source>本机没有可用的浏览器引擎，请安装 Microsoft Edge 或 Google Chrome 后重试。</source>
            <translation>No usable browser engine was found on this machine. Please install Microsoft Edge or Google Chrome and try again.</translation>
        </message>
        <message>
            <source>启动浏览器引擎 %1 失败：找不到可执行文件或被系统拒绝。</source>
            <translation>Failed to launch the browser engine %1: the executable was not found or was blocked by the system.</translation>
        </message>
        <message>
            <source>浏览器引擎 %1 退出码 %2，没有生成 PDF。</source>
            <translation>The browser engine %1 exited with code %2 without producing a PDF.</translation>
        </message>
        <message>
            <source>等了 %1 秒还没出结果，已终止浏览器进程。</source>
            <translation>Waited %1 seconds without a result; the browser process was terminated.</translation>
        </message>
        <message>
            <source>浏览器没有生成 PDF（产物是空的）。</source>
            <translation>The browser did not produce a PDF (the output is empty).</translation>
        </message>
        <message>
            <source>浏览器生成的 PDF 打不开。</source>
            <translation>The PDF produced by the browser cannot be opened.</translation>
        </message>
        <message>
            <source>浏览器生成的 PDF 里没有内容页。</source>
            <translation>The PDF produced by the browser has no content pages.</translation>
        </message>
        <message>
            <source>没有可打印的网页：既没有网址也没有 HTML 内容。</source>
            <translation>No web page to print: neither a URL nor HTML content was provided.</translation>
        </message>
        <message>
            <source>没有可用的打印工作区。</source>
            <translation>No printable workspace is available.</translation>
        </message>
        <message>
            <source>建临时目录失败：%1</source>
            <translation>Failed to create the temporary directory: %1</translation>
        </message>
        <message>
            <source>建浏览器临时 profile 目录失败。</source>
            <translation>Failed to create the temporary browser profile directory.</translation>
        </message>
        <message>
            <source>写临时 HTML 文件失败：%1</source>
            <translation>Failed to write the temporary HTML file: %1</translation>
        </message>
        <message>
            <source>临时 HTML 文件没写完整。</source>
            <translation>The temporary HTML file was not written completely.</translation>
        </message>
    </context>
<context>
        <name>aipdf::doc::CitationVerifier</name>
        <message>
            <source>引用 %1 没有给出原文，无法核对</source>
            <translation>Citation [%1] gives no quoted text, so it cannot be verified</translation>
        </message>
        <message>
            <source>引用 %1 的引文在原文中找不到</source>
            <translation>The text quoted by citation [%1] does not appear in the source</translation>
        </message>
        <message>
            <source>引用 %1 的页码与原文不符（原文在第 %2 页）</source>
            <translation>Citation [%1] points at the wrong page (the source is on page %2)</translation>
        </message>
        <message>
            <source>引用编号 %1 不在本次提供的材料中</source>
            <translation>Citation [%1] is not in the material provided for this answer</translation>
        </message>
    </context>
    <context>
        <name>aipdf::doc::EditCommand</name>
        <message>
            <source>修改%1</source>
            <translation>Edit %1</translation>
        </message>
        <message>
            <source>删除%1</source>
            <translation>Delete %1</translation>
        </message>
        <message>
            <source>批注命令不是合法的 JSON：%1</source>
            <translation>The annotation command is not valid JSON: %1</translation>
        </message>
        <message>
            <source>批注命令类型不匹配</source>
            <translation>The annotation command type does not match</translation>
        </message>
        <message>
            <source>批注命令里的前后批注不是同一条</source>
            <translation>The before and after annotations in the command are not the same annotation</translation>
        </message>
        <message>
            <source>批注命令里的批注内容不完整</source>
            <translation>The annotation data in the command is incomplete</translation>
        </message>
        <message>
            <source>未知的批注命令类型：%1</source>
            <translation>Unknown annotation command type: %1</translation>
        </message>
        <message>
            <source>添加%1</source>
            <translation>Add %1</translation>
        </message>
    </context>
    <context>
        <name>aipdf::doc::PageCommand</name>
        <message>
            <source>替换页面</source>
            <translation>Replace page</translation>
        </message>

        <message>
            <source>删除页</source>
            <translation>Delete page</translation>
        </message>
        <message>
            <source>删除页命令的页序号不合法</source>
            <translation>The delete-page command has an invalid page index</translation>
        </message>
        <message>
            <source>删除页命令缺少页序号</source>
            <translation>The delete-page command is missing the page index</translation>
        </message>
        <message>
            <source>插入空白页</source>
            <translation>Insert blank page</translation>
        </message>
        <message>
            <source>插入页命令的参数不合法</source>
            <translation>The insert-page command has invalid parameters</translation>
        </message>
        <message>
            <source>插入页命令缺少页序号或尺寸</source>
            <translation>The insert-page command is missing the page index or size</translation>
        </message>
        <message>
            <source>旋转页</source>
            <translation>Rotate page</translation>
        </message>
        <message>
            <source>旋转页命令的页序号不合法</source>
            <translation>The rotate-page command has an invalid page index</translation>
        </message>
        <message>
            <source>旋转页命令缺少页序号或角度</source>
            <translation>The rotate-page command is missing the page index or angle</translation>
        </message>
        <message>
            <source>移动页</source>
            <translation>Move page</translation>
        </message>
        <message>
            <source>移动页命令的页序号不合法</source>
            <translation>The move-page command has an invalid page index</translation>
        </message>
        <message>
            <source>移动页命令缺少页序号</source>
            <translation>The move-page command is missing the page index</translation>
        </message>
        <message>
            <source>页面命令不是合法的 JSON：%1</source>
            <translation>The page command is not valid JSON: %1</translation>
        </message>
        <message>
            <source>页面命令类型不匹配</source>
            <translation>The page command type does not match</translation>
        </message>
    </context>
    <context>
        <name>aipdf::doc::SafeSave</name>
        <message>
            <source>内容为空</source>
            <translation>The content is empty</translation>
        </message>
        <message>
            <source>写入临时文件失败：%1</source>
            <translation>Failed to write the temporary file: %1</translation>
        </message>
        <message>
            <source>写入临时文件失败：磁盘可能已满</source>
            <translation>Failed to write the temporary file: the disk may be full</translation>
        </message>
        <message>
            <source>写出的文件打不开：%1</source>
            <translation>The written file cannot be opened: %1</translation>
        </message>
        <message>
            <source>写出的文件页数不对：期望 %1 页，实际 %2 页</source>
            <translation>The written file has the wrong page count: expected %1, got %2</translation>
        </message>
        <message>
            <source>无法写入临时文件：%1</source>
            <translation>Cannot write the temporary file: %1</translation>
        </message>
        <message>
            <source>无法替换原文件</source>
            <translation>Could not replace the original file</translation>
        </message>
        <message>
            <source>无法替换原文件：%1</source>
            <translation>Could not replace the original file: %1</translation>
        </message>
        <message>
            <source>无法读取临时文件：%1</source>
            <translation>Cannot read the temporary file: %1</translation>
        </message>
        <message>
            <source>没有目标路径</source>
            <translation>No target path</translation>
        </message>
        <message>
            <source>目标目录不存在：%1</source>
            <translation>The target directory does not exist: %1</translation>
        </message>
        <message>
            <source>系统错误 %1</source>
            <translation>System error %1</translation>
        </message>
    </context>
    <context>
        <name>aipdf::doc::UndoStack</name>
        <message>
            <source>撤销 %1</source>
            <translation>Undo %1</translation>
        </message>
        <message>
            <source>重做 %1</source>
            <translation>Redo %1</translation>
        </message>
    </context>
    <context>
        <name>aipdf::ipc::WorkerClient</name>
        <message>
            <source>AI Worker 未连接，请稍后再试。</source>
            <translation>The AI Worker is not connected. Try again shortly.</translation>
        </message>
        <message>
            <source>AI Worker 请求失败。</source>
            <translation>The AI Worker request failed.</translation>
        </message>
        <message>
            <source>AI Worker 请求超时。</source>
            <translation>The AI Worker request timed out.</translation>
        </message>
    </context>
    <context>
        <name>aipdf::ocr::OcrService</name>
        <message>
            <source>没有可用的 OCR 引擎</source>
            <translation>No OCR engine is available</translation>
        </message>
    </context>
    <context>
        <name>aipdf::ocr::TesseractCliEngine</name>
        <message>
            <source>tesseract 启动失败</source>
            <translation>Failed to start Tesseract</translation>
        </message>
        <message>
            <source>tesseract 退出码 %1</source>
            <translation>Tesseract exited with code %1</translation>
        </message>
        <message>
            <source>无法创建临时目录</source>
            <translation>Could not create a temporary directory</translation>
        </message>
        <message>
            <source>没有找到 tesseract 可执行文件</source>
            <translation>The Tesseract executable was not found</translation>
        </message>
        <message>
            <source>识别超时（%1 ms）</source>
            <translation>Recognition timed out (%1 ms)</translation>
        </message>
        <message>
            <source>页面图像为空</source>
            <translation>The page image is empty</translation>
        </message>
        <message>
            <source>页面图像写盘失败</source>
            <translation>Failed to write the page image to disk</translation>
        </message>
    </context>
    <context>
        <name>aipdf::ocr::WindowsOcrEngine</name>
        <message>
            <source>OCR 脚本写入失败</source>
            <translation>Failed to write the OCR script</translation>
        </message>
        <message>
            <source>powershell 启动失败</source>
            <translation>Failed to start PowerShell</translation>
        </message>
        <message>
            <source>找不到 powershell，无法调用系统 OCR</source>
            <translation>PowerShell was not found, so the system OCR cannot be used</translation>
        </message>
        <message>
            <source>无法创建临时目录</source>
            <translation>Could not create a temporary directory</translation>
        </message>
        <message>
            <source>没有可用的系统 OCR 语言</source>
            <translation>No system OCR language is available</translation>
        </message>
        <message>
            <source>系统 OCR 退出码 %1</source>
            <translation>The system OCR exited with code %1</translation>
        </message>
        <message>
            <source>识别超时</source>
            <translation>Recognition timed out</translation>
        </message>
        <message>
            <source>页面图像为空</source>
            <translation>The page image is empty</translation>
        </message>
        <message>
            <source>页面图像写盘失败</source>
            <translation>Failed to write the page image to disk</translation>
        </message>
    </context>
    <context>
        <name>aipdf::pdf::Annotation</name>
        <message>
            <source>下划线</source>
            <translation>Underline</translation>
        </message>
        <message>
            <source>云线</source>
            <translation>Cloud</translation>
        </message>
        <message>
            <source>便签</source>
            <translation>Note</translation>
        </message>
        <message>
            <source>删除线</source>
            <translation>Strikethrough</translation>
        </message>
        <message>
            <source>图章</source>
            <translation>Stamp</translation>
        </message>
        <message>
            <source>墨迹</source>
            <translation>Ink</translation>
        </message>
        <message>
            <source>声音</source>
            <translation>Sound</translation>
        </message>
        <message>
            <source>多边形</source>
            <translation>Polygon</translation>
        </message>
        <message>
            <source>批注</source>
            <translation>Annotation</translation>
        </message>
        <message>
            <source>文本框</source>
            <translation>Text box</translation>
        </message>
        <message>
            <source>椭圆</source>
            <translation>Ellipse</translation>
        </message>
        <message>
            <source>波浪线</source>
            <translation>Squiggly</translation>
        </message>
        <message>
            <source>测量</source>
            <translation>Measure</translation>
        </message>
        <message>
            <source>直线</source>
            <translation>Line</translation>
        </message>
        <message>
            <source>矩形</source>
            <translation>Rectangle</translation>
        </message>
        <message>
            <source>箭头</source>
            <translation>Arrow</translation>
        </message>
        <message>
            <source>附件</source>
            <translation>Attachment</translation>
        </message>
        <message>
            <source>高亮</source>
            <translation>Highlight</translation>
        </message>
    </context>
    <context>
        <name>aipdf::pdf::AnnotationSummary</name>
        <message>
            <source>作者</source>
            <translation>Author</translation>
        </message>
        <message>
            <source>共 %1 条批注 · %2 页 · 导出于 %3</source>
            <translation>%1 annotations · %2 pages · Exported on %3</translation>
        </message>
        <message>
            <source>内容</source>
            <translation>Content</translation>
        </message>
        <message>
            <source>时间</source>
            <translation>Date</translation>
        </message>
        <message>
            <source>类型</source>
            <translation>Type</translation>
        </message>
        <message>
            <source>这份文档还没有批注。</source>
            <translation>This document has no annotations yet.</translation>
        </message>
        <message>
            <source>页</source>
            <translation>Page</translation>
        </message>
    </context>
            <context>
        <name>aipdf::pdf::CertificateChain</name>
        <message>
            <source>未被吊销</source>
            <translation>Not revoked</translation>
        </message>
        <message>
            <source>已被吊销</source>
            <translation>Revoked</translation>
        </message>
        <message>
            <source>状态未知（没有 CRL/OCSP）</source>
            <translation>Unknown (no CRL/OCSP)</translation>
        </message>
        <message>
            <source>签名证书解析失败：数据不是有效的 X.509 证书。</source>
            <translation>Failed to parse the signer certificate: the data is not a valid X.509 certificate.</translation>
        </message>
        <message>
            <source>证书链构建失败：缺少中间证书或链结构不完整。</source>
            <translation>Failed to build the certificate chain: intermediate certificates are missing or the chain is incomplete.</translation>
        </message>
        <message>
            <source>证书链的根证书不在本机受信任的证书存储中。</source>
            <translation>The chain's root certificate is not in this computer's trusted certificate store.</translation>
        </message>
        <message>
            <source>证书链无法建立。</source>
            <translation>The certificate chain could not be built.</translation>
        </message>
        <message>
            <source>证书链上存在已被吊销的证书。</source>
            <translation>A certificate in the chain has been revoked.</translation>
        </message>
        <message>
            <source>证书链上存在不在有效期内的证书。</source>
            <translation>A certificate in the chain is outside its validity period.</translation>
        </message>
        <message>
            <source>证书链不在本机受信任的证书存储中。</source>
            <translation>The certificate chain is not trusted by this computer's certificate store.</translation>
        </message>
        <message>
            <source>证书链完整且受本机信任。</source>
            <translation>The certificate chain is complete and trusted by this computer.</translation>
        </message>
        <message>
            <source>CRL 分发点：%1</source>
            <translation>CRL distribution points: %1</translation>
        </message>
        <message>
            <source>OCSP 响应地址：%1</source>
            <translation>OCSP responder: %1</translation>
        </message>
        <message>
            <source>CA 吊销地址：%1</source>
            <translation>CA revocation URL: %1</translation>
        </message>
        <message>
            <source>证书声明无需检查 OCSP。</source>
            <translation>The certificate declares that OCSP checking is not required.</translation>
        </message>
    </context>
<context>
        <name>aipdf::pdf::DocumentReflow</name>
        <message>
            <source>%1 个文字块</source>
            <translation>%1 text blocks</translation>
        </message>
        <message>
            <source>%1 个图片对象</source>
            <translation>%1 image objects</translation>
        </message>
        <message>
            <source>%1 个矢量图形</source>
            <translation>%1 vector graphics</translation>
        </message>
        <message>
            <source>%1 个表格</source>
            <translation>%1 tables</translation>
        </message>
        <message>
            <source>%1 个对象</source>
            <translation>%1 objects</translation>
        </message>
        <message>
            <source>PDFium 建不出文字对象</source>
            <translation>PDFium could not create the text object</translation>
        </message>
        <message>
            <source>文字对象放不进页面</source>
            <translation>The text object does not fit on the page</translation>
        </message>
        <message>
            <source>PDFium 建不出表格边框</source>
            <translation>PDFium could not create the table border</translation>
        </message>
        <message>
            <source>表格边框放不进页面</source>
            <translation>The table border does not fit on the page</translation>
        </message>
        <message>
            <source>PDFium 打不开这份 PDF，重排失败</source>
            <translation>PDFium could not open this PDF; the reflow failed</translation>
        </message>
        <message>
            <source>新页面建不出来</source>
            <translation>The new page could not be created</translation>
        </message>
        <message>
            <source>第 %1 页上的图形对象找不到了，搬不过去</source>
            <translation>A graphics object on page %1 could not be found, so it cannot be moved</translation>
        </message>
        <message>
            <source>图形对象放不进新页面</source>
            <translation>The graphics object does not fit on the new page</translation>
        </message>
        <message>
            <source>PDFium 没有写出重排后的 PDF</source>
            <translation>PDFium did not write the reflowed PDF</translation>
        </message>
        <message>
            <source>这一段在新页宽下排不进去：%1</source>
            <translation>This paragraph does not fit at the new page width: %1</translation>
        </message>
        <message>
            <source>这张表在新页宽下放不下一整页，没能搬过去</source>
            <translation>This table does not fit on a whole page at the new width, so it was not moved</translation>
        </message>
        <message>
            <source>这个对象没有尺寸，搬不过去</source>
            <translation>This object has no size, so it cannot be moved</translation>
        </message>
        <message>
            <source>有 %1 段包含标准 14 号字体表示不了的字符，排版时按仓库既有做法写成 '?' 占位符</source>
            <translation>%1 paragraphs contain characters the standard 14 fonts cannot represent; they were written as '?' placeholders</translation>
        </message>
        <message>
            <source>重排结果写不出来</source>
            <translation>The reflow result could not be written</translation>
        </message>
        <message>
            <source>新文档没能重新打上标签：%1</source>
            <translation>The new document could not be tagged again: %1</translation>
        </message>
        <message>
            <source>本次构建没有启用 qpdf，新文档没能重新打上标签</source>
            <translation>This build has no qpdf, so the new document could not be tagged again</translation>
        </message>
        <message>
            <source>重排结果打不开，统计不了页面对象：%1</source>
            <translation>The reflow result cannot be opened, so page objects cannot be counted: %1</translation>
        </message>
        <message>
            <source>重排后少了 %1，已阻止写回</source>
            <translation>%1 is missing after the reflow; writing back was blocked</translation>
        </message>
        <message>
            <source>页眉「%1」</source>
            <translation>running head “%1”</translation>
        </message>
        <message>
            <source>页脚「%1」</source>
            <translation>running foot “%1”</translation>
        </message>
        <message>
            <source>重排后的版面与原文档不同：页数从 %1 页变成 %2 页</source>
            <translation>The reflowed layout differs from the original: the page count changed from %1 to %2</translation>
        </message>
        <message>
            <source>图片 %1 个、矢量图形 %2 个、表格 %3 个已按新版面搬过去</source>
            <translation>%1 images, %2 vector graphics and %3 tables were moved to the new layout</translation>
        </message>
        <message>
            <source>页眉 %1 处、页脚 %2 处已按每页重复放到每一页</source>
            <translation>%1 running heads and %2 running feet were repeated on every page</translation>
        </message>
        <message>
            <source>表格边框是新画的线，矢量图形的计数因此比原文档多</source>
            <translation>Table borders are newly drawn lines, so the vector graphics count is higher than in the source document</translation>
        </message>
        <message>
            <source>重排不保留书签与内部链接的目标：页码变了，那些指向已经旧了</source>
            <translation>The reflow does not preserve bookmark and internal link targets: page numbers changed, so those targets are stale</translation>
        </message>
    </context>
<context>
        <name>aipdf::pdf::DocumentWriter</name>
        <message>
            <source>文档序列化失败</source>
            <translation>Failed to serialize the document</translation>
        </message>
    </context>
    <context>
        <name>aipdf::pdf::IncrementalWriter</name>
        <message>
            <source>startxref 数值无效</source>
            <translation>Invalid startxref value</translation>
        </message>
        <message>
            <source>trailer 里没有可解析的 /Root</source>
            <translation>No parsable /Root in the trailer</translation>
        </message>
        <message>
            <source>回复引用的父批注不在本次写入中：</source>
            <translation>The parent annotation referenced by the reply is not part of this write: </translation>
        </message>
        <message>
            <source>所有外观流都为空，没有可写的内容</source>
            <translation>All appearance streams are empty; nothing to write</translation>
        </message>
        <message>
            <source>找不到 startxref：输入不是完整的 PDF</source>
            <translation>Cannot find startxref: the input is not a complete PDF</translation>
        </message>
        <message>
            <source>找不到 trailer：可能是交叉引用流，暂不支持</source>
            <translation>Cannot find trailer: possibly a cross-reference stream, not supported yet</translation>
        </message>
        <message>
            <source>找不到带标记 %1 的批注对象</source>
            <translation>Cannot find the annotation object with marker %1</translation>
        </message>
        <message>
            <source>插入点越界，放弃写入</source>
            <translation>Insertion point is out of range; aborting the write</translation>
        </message>
        <message>
            <source>没有要写入的外观流</source>
            <translation>No appearance streams to write</translation>
        </message>
        <message>
            <source>缺少批注标记，无法定位注释</source>
            <translation>Missing annotation marker; cannot locate the annotation</translation>
        </message>
    </context>
        <context>
        <name>aipdf::pdf::MediaSource</name>
        <message>
            <source>不是媒体文件：%1</source>
            <translation>Not a media file: %1</translation>
        </message>
        <message>
            <source>内存不够</source>
            <translation>Out of memory</translation>
        </message>
        <message>
            <source>创建媒体类型失败</source>
            <translation>Failed to create the media type</translation>
        </message>
        <message>
            <source>参数不对</source>
            <translation>Invalid argument</translation>
        </message>
        <message>
            <source>只支持 16 位 PCM 的音频</source>
            <translation>Only 16-bit PCM audio is supported</translation>
        </message>
        <message>
            <source>找不到这个文件</source>
            <translation>File not found</translation>
        </message>
        <message>
            <source>找不到这个路径</source>
            <translation>Path not found</translation>
        </message>
        <message>
            <source>文件打不开：%1</source>
            <translation>Cannot open file: %1</translation>
        </message>
        <message>
            <source>文件打不开：%1（%2）</source>
            <translation>Cannot open file: %1 (%2)</translation>
        </message>
        <message>
            <source>文件打不开：没有读这个文件的权限（%1）</source>
            <translation>Cannot open file: no permission to read it (%1)</translation>
        </message>
        <message>
            <source>文件格式不对</source>
            <translation>Wrong file format</translation>
        </message>
        <message>
            <source>文件里的数据不完整</source>
            <translation>The data in the file is incomplete</translation>
        </message>
        <message>
            <source>无法把视频轨转换成 RGB32：%1</source>
            <translation>Cannot convert the video track to RGB32: %1</translation>
        </message>
        <message>
            <source>无法把音频轨转换成 PCM：%1</source>
            <translation>Cannot convert the audio track to PCM: %1</translation>
        </message>
        <message>
            <source>没有给出文件路径</source>
            <translation>No file path was given</translation>
        </message>
        <message>
            <source>没有读这个文件的权限</source>
            <translation>No permission to read this file</translation>
        </message>
        <message>
            <source>系统不接受这个文件格式</source>
            <translation>The system does not accept this file format</translation>
        </message>
        <message>
            <source>系统不认识这个文件的格式</source>
            <translation>The system does not recognize this file format</translation>
        </message>
        <message>
            <source>系统媒体组件已经关闭</source>
            <translation>The system media components are already shut down</translation>
        </message>
        <message>
            <source>系统媒体组件还没初始化</source>
            <translation>The system media components are not initialized yet</translation>
        </message>
        <message>
            <source>系统媒体组件（Media Foundation）初始化失败</source>
            <translation>Failed to initialize the system media components (Media Foundation)</translation>
        </message>
        <message>
            <source>系统返回了一个未知的错误</source>
            <translation>The system returned an unknown error</translation>
        </message>
        <message>
            <source>解码器还没有打开</source>
            <translation>The decoder is not open yet</translation>
        </message>
        <message>
            <source>解码失败：%1</source>
            <translation>Decoding failed: %1</translation>
        </message>
        <message>
            <source>解码失败：取不到声音数据（%1）</source>
            <translation>Decoding failed: cannot get the audio data (%1)</translation>
        </message>
        <message>
            <source>解码失败：取不到画面数据（%1）</source>
            <translation>Decoding failed: cannot get the video data (%1)</translation>
        </message>
        <message>
            <source>解码失败：画面尺寸和实际数据对不上</source>
            <translation>Decoding failed: the frame size does not match the actual data</translation>
        </message>
        <message>
            <source>解码失败：画面换算失败</source>
            <translation>Decoding failed: the frame could not be converted</translation>
        </message>
        <message>
            <source>解码失败：读不出声音数据（%1）</source>
            <translation>Decoding failed: cannot read the audio data (%1)</translation>
        </message>
        <message>
            <source>解码失败：读不出画面数据（%1）</source>
            <translation>Decoding failed: cannot read the video data (%1)</translation>
        </message>
        <message>
            <source>这个文件正被别的程序占用</source>
            <translation>This file is in use by another program</translation>
        </message>
    </context>
<context>
        <name>aipdf::pdf::PdfiumAdapter</name>
        <message>
            <source>PDFium 保存失败</source>
            <translation>PDFium failed to save</translation>
        </message>
        <message>
            <source>PDFium 未知错误</source>
            <translation>Unknown PDFium error</translation>
        </message>
        <message>
            <source>PDFium 错误码 %1</source>
            <translation>PDFium error code %1</translation>
        </message>
        <message>
            <source>不支持的安全处理器</source>
            <translation>Unsupported security handler</translation>
        </message>
        <message>
            <source>内容为空</source>
            <translation>The content is empty</translation>
        </message>
        <message>
            <source>内容过大，拒绝加载</source>
            <translation>The content is too large; refusing to load</translation>
        </message>
        <message>
            <source>文件不存在或无法读取</source>
            <translation>The file does not exist or cannot be read</translation>
        </message>
        <message>
            <source>文件不存在：%1</source>
            <translation>File not found: %1</translation>
        </message>
        <message>
            <source>文件不是有效的 PDF</source>
            <translation>The file is not a valid PDF</translation>
        </message>
        <message>
            <source>文件过大，拒绝加载</source>
            <translation>The file is too large; refusing to load</translation>
        </message>
        <message>
            <source>文件过大：%1 字节，上限 %2 字节</source>
            <translation>File too large: %1 bytes (limit %2 bytes)</translation>
        </message>
        <message>
            <source>文档无效</source>
            <translation>The document is invalid</translation>
        </message>
        <message>
            <source>文档的页面无法加载（文件可能已损坏）</source>
            <translation>The document's pages could not be loaded (the file may be damaged)</translation>
        </message>
        <message>
            <source>文档里没有任何页面（文件可能已损坏）</source>
            <translation>The document has no pages (the file may be damaged)</translation>
        </message>
        <message>
            <source>无法读取文件：%1</source>
            <translation>Cannot read the file: %1</translation>
        </message>
        <message>
            <source>需要密码，或密码不正确</source>
            <translation>A password is required, or the password is incorrect</translation>
        </message>
        <message>
            <source>页码越界</source>
            <translation>Page number out of range</translation>
        </message>
    </context>
    <context>
        <name>aipdf::pdf::QpdfBackend</name>
        <message>
            <source>qpdf 报告了结构问题但没有给出细节</source>
            <translation>qpdf reported a structural problem without details</translation>
        </message>
        <message>
            <source>qpdf 报告结构问题：%1</source>
            <translation>qpdf reported a structural problem: %1</translation>
        </message>
        <message>
            <source>qpdf 解析失败：%1</source>
            <translation>qpdf parsing failed: %1</translation>
        </message>
        <message>
            <source>没有可校验的内容</source>
            <translation>Nothing to verify</translation>
        </message>
    </context>
        <context>
        <name>aipdf::pdf::SemanticTagWriter</name>
        <message>
            <source>输入的 PDF 内容为空</source>
            <translation>The input PDF content is empty</translation>
        </message>
        <message>
            <source>语义树是空的，没有可写的结构元素</source>
            <translation>The semantic tree is empty; there are no structure elements to write</translation>
        </message>
        <message>
            <source>写语义标签树失败</source>
            <translation>Writing the semantic tag tree failed</translation>
        </message>
    </context>
<context>
        <name>aipdf::pdf::SoundFormat</name>
        <message>
            <source>data 块出现在 fmt 之前</source>
            <translation>The data chunk appears before the fmt chunk</translation>
        </message>
        <message>
            <source>fmt 块不完整</source>
            <translation>The fmt chunk is incomplete</translation>
        </message>
        <message>
            <source>不是 RIFF/WAVE 文件</source>
            <translation>Not a RIFF/WAVE file</translation>
        </message>
        <message>
            <source>只支持 PCM（format=1），当前是 %1</source>
            <translation>Only PCM (format=1) is supported; got %1</translation>
        </message>
        <message>
            <source>文件太小，不是 WAV</source>
            <translation>The file is too small to be a WAV</translation>
        </message>
        <message>
            <source>格式参数非法</source>
            <translation>Invalid format parameters</translation>
        </message>
        <message>
            <source>没有找到 data 块</source>
            <translation>No data chunk found</translation>
        </message>
    </context>
        <context>
        <name>aipdf::pdf::UaGenerator</name>
        <message>
            <source>输入的 PDF 内容为空</source>
            <translation>The input PDF content is empty</translation>
        </message>
        <message>
            <source>扫描 PDF 内容失败</source>
            <translation>Scanning the PDF content failed</translation>
        </message>
    </context>
    <context>
        <name>aipdf::pdf::VeraPdfChecker</name>
        <message>
            <source>本机没有找到 veraPDF。合规认证要用 veraPDF（Java 程序），本仓库不自带它：装好并加入 PATH，或在界面上填它的完整路径。</source>
            <translation>veraPDF was not found on this machine. Compliance certification needs veraPDF (a Java program); this repository does not bundle it: install it and add it to PATH, or enter its full path in the app.</translation>
        </message>
        <message>
            <source>没有找到 veraPDF，没法做合规认证。</source>
            <translation>veraPDF was not found, so compliance certification is not possible.</translation>
        </message>
        <message>
            <source>没有待认证的文件。</source>
            <translation>There is no file to certify.</translation>
        </message>
        <message>
            <source>veraPDF 没有给出报告。</source>
            <translation>veraPDF produced no report.</translation>
        </message>
        <message>
            <source>veraPDF 的报告读不出来：%1</source>
            <translation>The veraPDF report could not be read: %1</translation>
        </message>
        <message>
            <source>veraPDF 的报告里没有逐条结论，读不出认证结果。</source>
            <translation>The veraPDF report contains no per-rule findings, so no verdict could be derived.</translation>
        </message>
        <message>
            <source>veraPDF 判定这份文件合规（%1 条规则全部通过）。</source>
            <translation>veraPDF judged this file compliant (%1 rules, all passed).</translation>
        </message>
        <message>
            <source>veraPDF 判定这份文件不合规：%1 条规则没通过。</source>
            <translation>veraPDF judged this file non-compliant: %1 rule(s) failed.</translation>
        </message>
        <message>
            <source>本机没有装 veraPDF，本程序做不了 PDF/A 合规认证。本程序自带的 PDF/A 预检只看文件结构里能读出来的要求，**预检通过不等于合规**——认证结论只能由 veraPDF 给。</source>
            <translation>veraPDF is not installed, so this program cannot certify PDF/A compliance. The built-in PDF/A preflight only checks the requirements that can be read from the file structure; a passing preflight is not compliance. Only veraPDF can give a certification verdict.</translation>
        </message>
    </context>
<context>
        <name>aipdf::tools::ProposalValidator</name>
        <message>
            <source>%1 不接受参数 %2</source>
            <translation>%1 does not accept the parameter %2</translation>
        </message>
        <message>
            <source>%1 是只读工具，不能出现在变更提案里</source>
            <translation>%1 is a read-only tool and cannot appear in a change proposal</translation>
        </message>
        <message>
            <source>%1 缺少必填参数 %2</source>
            <translation>%1 is missing the required parameter %2</translation>
        </message>
        <message>
            <source>args 必须是对象</source>
            <translation>args must be an object</translation>
        </message>
        <message>
            <source>一次最多 %1 条操作</source>
            <translation>At most %1 operations per proposal</translation>
        </message>
        <message>
            <source>删除第 %1 页</source>
            <translation>Delete page %1</translation>
        </message>
        <message>
            <source>参数 %1 元素太多</source>
            <translation>Parameter %1 has too many elements</translation>
        </message>
        <message>
            <source>参数 %1 元素太少</source>
            <translation>Parameter %1 has too few elements</translation>
        </message>
        <message>
            <source>参数 %1 小于下限 %2</source>
            <translation>Parameter %1 is below the minimum %2</translation>
        </message>
        <message>
            <source>参数 %1 类型应为 %2</source>
            <translation>Parameter %1 must be of type %2</translation>
        </message>
        <message>
            <source>参数 %1 超过上限 %2</source>
            <translation>Parameter %1 exceeds the maximum %2</translation>
        </message>
        <message>
            <source>在第 %1 页前插入空白页</source>
            <translation>Insert a blank page before page %1</translation>
        </message>
        <message>
            <source>在第 %1 页添加批注：%2</source>
            <translation>Add a note on page %1: %2</translation>
        </message>
        <message>
            <source>工具 %1 被禁止：AI 不能执行文件、Shell 或网络操作</source>
            <translation>Tool %1 is forbidden: AI cannot perform file, shell, or network operations</translation>
        </message>
        <message>
            <source>把第 %1 页移动到第 %2 位</source>
            <translation>Move page %1 to position %2</translation>
        </message>
        <message>
            <source>提案里没有任何操作</source>
            <translation>The proposal contains no operations</translation>
        </message>
        <message>
            <source>插入位置 %1 超出范围（1–%2）</source>
            <translation>Insert position %1 is out of range (1–%2)</translation>
        </message>
        <message>
            <source>操作必须是对象</source>
            <translation>Each operation must be an object</translation>
        </message>
        <message>
            <source>文档已被修改，请重新生成提案后再确认</source>
            <translation>The document has changed. Regenerate the proposal and confirm again.</translation>
        </message>
        <message>
            <source>未知工具：%1</source>
            <translation>Unknown tool: %1</translation>
        </message>
        <message>
            <source>缺少 tool 字段</source>
            <translation>The tool field is missing</translation>
        </message>
        <message>
            <source>页码 %1 超出范围（1–%2）</source>
            <translation>Page %1 is out of range (1–%2)</translation>
        </message>
        <message>
            <source>页码超出范围（1–%1）</source>
            <translation>Page out of range (1–%1)</translation>
        </message>
    </context>
    <context>
        <name>aipdf::tools::ToolRegistry</name>
        <message>
            <source>删除页面</source>
            <translation>Delete pages</translation>
        </message>
        <message>
            <source>找出空白页</source>
            <translation>Find blank pages</translation>
        </message>
        <message>
            <source>插入空白页</source>
            <translation>Insert blank page</translation>
        </message>
        <message>
            <source>添加批注草稿</source>
            <translation>Add annotation draft</translation>
        </message>
        <message>
            <source>移动页面</source>
            <translation>Move page</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::EncryptionDialog</name>
        <message>
            <source>取消</source>
            <translation>Cancel</translation>
        </message>
        <message>
            <source>保存时用 AES-256 加密这份文档</source>
            <translation>Encrypt this document with AES-256 when saving</translation>
        </message>
        <message>
            <source>打开密码（留空表示不需要密码）</source>
            <translation>Open password (leave empty for no password)</translation>
        </message>
        <message>
            <source>确认打开密码</source>
            <translation>Confirm open password</translation>
        </message>
        <message>
            <source>所有者密码（留空自动生成）</source>
            <translation>Owner password (leave empty to generate one)</translation>
        </message>
        <message>
            <source>打开密码为空：文件可以直接打开，权限位只对遵守规范的阅读器生效。</source>
            <translation>The open password is empty: the file opens without a password, and the permission flags only apply to readers that honor them.</translation>
        </message>
        <message>
            <source>所有者密码用于修改权限；留空时由程序随机生成，无法事后解除限制。</source>
            <translation>The owner password protects permission changes; if left empty, a random one is generated and the restrictions cannot be lifted later.</translation>
        </message>
        <message>
            <source>允许打印</source>
            <translation>Allow printing</translation>
        </message>
        <message>
            <source>允许高质量打印</source>
            <translation>Allow high-quality printing</translation>
        </message>
        <message>
            <source>允许复制文本与图片</source>
            <translation>Allow copying text and images</translation>
        </message>
        <message>
            <source>允许添加批注</source>
            <translation>Allow adding annotations</translation>
        </message>
        <message>
            <source>允许填写表单</source>
            <translation>Allow filling forms</translation>
        </message>
        <message>
            <source>允许插入、删除、旋转页面</source>
            <translation>Allow inserting, deleting, and rotating pages</translation>
        </message>
        <message>
            <source>允许其它修改</source>
            <translation>Allow other modifications</translation>
        </message>
        <message>
            <source>无障碍提取在 PDF 2.0 中始终允许，无法关闭；这是规范要求，不是遗漏。</source>
            <translation>Accessibility extraction is always allowed in PDF 2.0 and cannot be disabled; this is required by the specification.</translation>
        </message>
        <message>
            <source>确定</source>
            <translation>OK</translation>
        </message>
        <message>
            <source>加密与权限</source>
            <translation>Encryption and permissions</translation>
        </message>
        <message>
            <source>两次输入的打开密码不一致。</source>
            <translation>The two open passwords do not match.</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::AiSettingsDialog</name>
        <message><source>自定义 OpenAI 兼容服务</source><translation>Custom OpenAI-compatible service</translation></message>
    </context>
    <context>
        <name>aipdf::app::MainWindow</name>
        <message><source>书签已重命名</source><translation>Bookmark renamed</translation></message>
        <message><source>书签标题:</source><translation>Bookmark title:</translation></message>
        <message><source>删除书签</source><translation>Delete bookmark</translation></message>
        <message><source>删除选中书签</source><translation>Delete selected bookmark</translation></message>
        <message><source>已删除书签</source><translation>Bookmark deleted</translation></message>
        <message><source>已添加书签“%1”</source><translation>Added bookmark “%1”</translation></message>
        <message><source>已重置 %1 个表单字段</source><translation>Reset %1 form fields</translation></message>
        <message><source>建域</source><translation>Create fields</translation></message>
        <message><source>打开书签面板</source><translation>Open bookmarks panel</translation></message>
        <message><source>数据</source><translation>Data</translation></message>
        <message><source>文本工具</source><translation>Text tools</translation></message>
        <message><source>替换图片</source><translation>Replace image</translation></message>
        <message><source>模板</source><translation>Templates</translation></message>
        <message><source>添加书签</source><translation>Add bookmark</translation></message>
        <message><source>添加当前页书签</source><translation>Bookmark current page</translation></message>
        <message><source>网址链接</source><translation>Web link</translation></message>
        <message><source>设置已保存</source><translation>Settings saved</translation></message>
        <message><source>识别</source><translation>Recognition</translation></message>
        <message><source>请先在书签面板中选择一个书签。</source><translation>Select a bookmark in the bookmarks panel first.</translation></message>
        <message><source>请先打开一份 PDF。</source><translation>Open a PDF first.</translation></message>
        <message><source>重命名书签</source><translation>Rename bookmark</translation></message>
        <message><source>重命名选中书签</source><translation>Rename selected bookmark</translation></message>
        <message><source>链接</source><translation>Links</translation></message>
        <message><source>页面链接</source><translation>Page link</translation></message>
        <message><source>删除图片</source><translation>Delete image</translation></message>
        <message><source>第 %1 页</source><translation>Page %1</translation></message>
        <message><source>附件</source><translation>Attachments</translation></message>
        <message><source>字段属性…</source><translation>Field properties…</translation></message>
        <message><source>服务</source><translation>Services</translation></message>
        <message><source>流程</source><translation>Workflow</translation></message>
        <message><source>签章</source><translation>Seal</translation></message>
        <message><source>签署</source><translation>Sign</translation></message>
    </context>
    <context>
        <name>aipdf::app::PdfView</name>
        <message><source>先选中要添加链接的文字或区域。</source><translation>Select the text or area for the link first.</translation></message>
        <message><source>词表 %1 加载失败</source><translation>Failed to load dictionary: %1</translation></message>
        <message>
            <source>以下词表没有加载成功，本次检查没用到它们：
%1</source>
            <translation>The following dictionaries failed to load and were not used in this check:
%1</translation>
        </message>
        <message><source>已创建 %1 个网络链接</source><translation>Created %1 web links</translation></message>
        <message><source>已替换 %1 处</source><translation>Replaced %1 occurrences</translation></message>
        <message><source>已添加跳转到第 %1 页的链接</source><translation>Added a link to page %1</translation></message>
        <message><source>已添加链接</source><translation>Link added</translation></message>
        <message><source>搜索替换</source><translation>Find and replace</translation></message>
        <message><source>整个文档</source><translation>Entire document</translation></message>
        <message><source>新文字:</source><translation>New text:</translation></message>
        <message><source>替换为:</source><translation>Replace with:</translation></message>
        <message><source>替换范围:</source><translation>Replace scope:</translation></message>
        <message><source>查找内容:</source><translation>Find:</translation></message>
        <message><source>没有发现需要添加的网址链接。</source><translation>No web addresses need links.</translation></message>
        <message><source>没有打开的文档。</source><translation>No document is open.</translation></message>
        <message><source>没有找到匹配内容。</source><translation>No matching text was found.</translation></message>
        <message><source>添加链接</source><translation>Add link</translation></message>
        <message><source>网址:</source><translation>Web address:</translation></message>
        <message><source>网络链接</source><translation>Web links</translation></message>
        <message><source>请输入有效的网址。</source><translation>Enter a valid web address.</translation></message>
        <message><source>跳转到页码:</source><translation>Go to page:</translation></message>
        <message><source>链接没有写入页面。</source><translation>The link could not be written to the page.</translation></message>
        <message><source>页面链接</source><translation>Page link</translation></message>
        <message><source>当前页</source><translation>Current page</translation></message>
        <message><source>请先打开一份 PDF。</source><translation>Open a PDF first.</translation></message>
        <message><source>创建表单域</source><translation>Create form field</translation></message>
        <message><source>字段写入后校验失败：%1</source><translation>Field validation failed after writing: %1</translation></message>
        <message><source>字段名称:</source><translation>Field name:</translation></message>
        <message><source>字段名称已存在，请换一个名称。</source><translation>The field name already exists. Choose another name.</translation></message>
        <message><source>已在第 %1 页创建表单字段“%2”</source><translation>Created form field “%2” on page %1</translation></message>
        <message><source>无法序列化文档：%1</source><translation>Could not serialize the document: %1</translation></message>
        <message><source>允许多行</source><translation>Allow multiple lines</translation></message>
        <message><source>只读</source><translation>Read-only</translation></message>
        <message><source>字段名称</source><translation>Field name</translation></message>
        <message><source>字段名称不能为空。</source><translation>Field name cannot be empty.</translation></message>
        <message><source>字段名称已存在。</source><translation>The field name already exists.</translation></message>
        <message><source>字段属性</source><translation>Field properties</translation></message>
        <message><source>字段重命名需要同步计算脚本引用，当前保持原名称。</source><translation>Renaming a field requires updating calculation script references; the original name has been kept.</translation></message>
        <message><source>工具提示</source><translation>Tooltip</translation></message>
        <message><source>必填</source><translation>Required</translation></message>
        <message><source>每行一个选项</source><translation>One option per line</translation></message>
        <message><source>电子签章</source><translation>Electronic signatures</translation></message>
        <message><source>签章图片无效，无法放置。</source><translation>The seal image is invalid and cannot be placed.</translation></message>
        <message><source>签章已放置；保存文档后写入 PDF。</source><translation>Seal placed; save the document to write it to the PDF.</translation></message>
        <message><source>请在页面上单击签章位置；按其他工具可取消。</source><translation>Click the position for the seal on the page; choose another tool to cancel.</translation></message>
        <message><source>请在页面上拖出表单字段的位置；按其他工具可取消。</source><translation>Drag the form field area on the page; choose another tool to cancel.</translation></message>
        <message><source>选项</source><translation>Options</translation></message>
        <message><source>骑缝章只写入了 %1/%2 页。</source><translation>The cross-page seal was written to only %1 of %2 pages.</translation></message>
        <message><source>骑缝章已拆分到 %1 页；保存文档后写入 PDF。</source><translation>The cross-page seal was split across %1 pages; save the document to write it to the PDF.</translation></message>
        <message><source>骑缝章：%1 (%2/%3)</source><translation>Cross-page seal: %1 (%2/%3)</translation></message>
    </context>
    <context>
        <name>aipdf::app::SignatureDialog</name>
        <message><source>使用此签章</source><translation>Use this seal</translation></message>
        <message><source>删除签章</source><translation>Delete seal</translation></message>
        <message><source>导入图片…</source><translation>Import image…</translation></message>
        <message><source>导入签章</source><translation>Import seal</translation></message>
        <message><source>导入签章图片</source><translation>Import seal image</translation></message>
        <message><source>我的签名</source><translation>My signature</translation></message>
        <message><source>手写签名</source><translation>Handwritten signature</translation></message>
        <message><source>手写签名…</source><translation>Handwritten signature…</translation></message>
        <message><source>按住鼠标或触控笔书写签名：</source><translation>Hold the mouse button or stylus down to write your signature:</translation></message>
        <message><source>无法保存签名。</source><translation>Could not save the signature.</translation></message>
        <message><source>无法保存签章图片。</source><translation>Could not save the seal image.</translation></message>
        <message><source>无法识别这张图片。</source><translation>This image could not be recognized.</translation></message>
        <message><source>清空</source><translation>Clear</translation></message>
        <message><source>确定删除“%1”吗？</source><translation>Delete “%1”?</translation></message>
        <message><source>签章仅保存在本机。可导入透明 PNG/JPEG，或直接手写创建。</source><translation>Seals are stored only on this device. Import a transparent PNG/JPEG or create a handwritten signature.</translation></message>
        <message><source>签章名称</source><translation>Seal name</translation></message>
        <message><source>管理签章</source><translation>Manage seals</translation></message>
        <message><source>选择签章</source><translation>Select a seal</translation></message>
        <message><source>删除</source><translation>Delete</translation></message>
        <message><source>名称</source><translation>Name</translation></message>
        <message><source>图片 (*.png *.jpg *.jpeg *.bmp)</source><translation>Images (*.png *.jpg *.jpeg *.bmp)</translation></message>
    </context>
    <context>
        <name>aipdf::app::AccessibilityAudit</name>
        <message><source>%1 项警告</source><translation>%1 warnings</translation></message>
        <message><source>Unicode 映射</source><translation>Unicode mapping</translation></message>
        <message><source>未发现上述基础无障碍问题。</source><translation>No basic accessibility issues listed above were found.</translation></message>
        <message><source>未命名</source><translation>Unnamed</translation></message>
        <message><source>检查项</source><translation>Check</translation></message>
        <message><source>第 %1 页存在未命名的表单域。</source><translation>Page %1 contains an unnamed form field.</translation></message>
        <message><source>第 %1 页有字符缺少 Unicode 映射。</source><translation>Some characters on page %1 lack Unicode mappings.</translation></message>
        <message><source>第 %1 页没有可提取文字，可能需要 OCR 或替代文本。</source><translation>Page %1 has no extractable text and may require OCR or alternative text.</translation></message>
        <message><source>第 %1 页的表单域“%2”缺少说明。</source><translation>Form field “%2” on page %1 has no description.</translation></message>
        <message><source>结果</source><translation>Result</translation></message>
        <message><source>表单域名称</source><translation>Form field names</translation></message>
        <message><source>表单域说明</source><translation>Form field descriptions</translation></message>
        <message><source>详细信息</source><translation>Details</translation></message>
        <message><source>说明：本检查不等同于 PDF/UA 合规认证，标签树、图片替代文本和语义结构仍需专业工具复核。</source><translation>Note: This check is not a PDF/UA compliance certification. Tag trees, image alternative text, and semantic structure still require review with specialist tools.</translation></message>
        <message><source>辅助功能检查报告</source><translation>Accessibility check report</translation></message>
        <message><source>通过</source><translation>Passed</translation></message>
        <message><source>页面文本层</source><translation>Page text layers</translation></message>
    </context>
    <context>
        <name>aipdf::app::MainWindow</name>
        <message><source>标签</source><translation>Tags</translation></message>
    </context>
    <context>
        <name>aipdf::app::PdfView</name>
        <message><source>HTML 文件 (*.html)</source><translation>HTML files (*.html)</translation></message>
        <message><source>以下内容按 PDF 文本层的逻辑顺序显示，可用于人工核对屏幕阅读器的阅读次序。</source><translation>The content below follows the logical order of the PDF text layer for manually checking screen-reader reading order.</translation></message>
        <message><source>保存失败</source><translation>Save failed</translation></message>
        <message><source>保存报告</source><translation>Save report</translation></message>
        <message><source>保存辅助功能报告</source><translation>Save accessibility report</translation></message>
        <message><source>完整检查</source><translation>Full check</translation></message>
        <message><source>当前页阅读顺序</source><translation>Current page reading order</translation></message>
        <message><source>本页没有可提取文字。</source><translation>This page has no extractable text.</translation></message>
        <message><source>辅助功能报告</source><translation>Accessibility report</translation></message>
        <message><source>辅助功能报告已保存</source><translation>Accessibility report saved</translation></message>
        <message><source>辅助功能检查</source><translation>Accessibility check</translation></message>
        <message><source>辅助功能检查结果</source><translation>Accessibility check results</translation></message>
        <message><source>阅读顺序</source><translation>Reading order</translation></message>
        <message><source>阅读顺序 - 第 %1 页</source><translation>Reading order — page %1</translation></message>
    </context>
    <context>
        <name>aipdf::app::SettingsDialog</name>
        <message><source> 磅</source><translation> pt</translation></message>
        <message><source>AI 设置</source><translation>AI settings</translation></message>
        <message><source>AI 配置目录：%1</source><translation>AI configuration directory: %1</translation></message>
        <message><source>Folixa · AI PDF Editor</source><translation>Folixa · AI PDF Editor</translation></message>
        <message><source>Folixa · 专注阅读，也为文档智能留出空间</source><translation>Folixa · Focused reading with room for document intelligence</translation></message>
        <message><source>PDF 渲染、文本索引和搜索默认在本机完成。只有执行云端 AI 功能时，才会按所选数据模式发送内容。</source><translation>PDF rendering, text indexing, and search run locally by default. Content is sent only when using cloud AI features, according to the selected data mode.</translation></message>
                <message>
            <source>不需要（本机模型）</source>
            <translation>Not needed (local model)</translation>
        </message>
        <message>
            <source>不需要</source>
            <translation>Not needed</translation>
        </message>
<message><source>专注于 PDF 阅读、编辑、检索与受控 AI 协作。</source><translation>Focused on PDF reading, editing, retrieval, and controlled AI collaboration.</translation></message>
        <message><source>了解文档内容如何在本地与 AI 服务之间流转。</source><translation>Learn how document content moves between your device and AI services.</translation></message>
        <message><source>关于</source><translation>About</translation></message>
        <message><source>启动时显示左侧导航栏</source><translation>Show the left navigation panel at startup</translation></message>
        <message><source>外观</source><translation>Appearance</translation></message>
        <message><source>字体</source><translation>Font</translation></message>
        <message><source>字号</source><translation>Font size</translation></message>
        <message><source>已安全保存</source><translation>Saved securely</translation></message>
        <message><source>常规</source><translation>General</translation></message>
        <message><source>打开文档后显示 AI 助手</source><translation>Show the AI assistant after opening a document</translation></message>
        <message><source>未设置</source><translation>Not configured</translation></message>
        <message><source>本地优先</source><translation>Local first</translation></message>
        <message><source>查看当前 AI 服务状态并进入详细配置。</source><translation>View the current AI service status and open detailed settings.</translation></message>
        <message><source>演示模式</source><translation>Presentation mode</translation></message>
        <message><source>版本 %1</source><translation>Version %1</translation></message>
        <message><source>界面语言</source><translation>Interface language</translation></message>
        <message><source>管理界面语言和工作区的启动状态。</source><translation>Manage the interface language and workspace startup state.</translation></message>
        <message><source>调整应用主题、界面字体和字号。</source><translation>Adjust the application theme, interface font, and font size.</translation></message>
        <message><source>跟随系统</source><translation>Follow system</translation></message>
        <message><source>配置 AI 服务…</source><translation>Configure AI service…</translation></message>
        <message><source>隐私与数据</source><translation>Privacy and data</translation></message>
        <message><source>预览</source><translation>Preview</translation></message>
        <message><source>API 密钥</source><translation>API key</translation></message>
        <message><source>主题</source><translation>Theme</translation></message>
        <message><source>暗色</source><translation>Dark</translation></message>
        <message><source>保存</source><translation>Save</translation></message>
        <message><source>取消</source><translation>Cancel</translation></message>
        <message><source>数据模式</source><translation>Data mode</translation></message>
        <message><source>服务商</source><translation>Provider</translation></message>
        <message><source>模型</source><translation>Model</translation></message>
        <message><source>浅色</source><translation>Light</translation></message>
        <message><source>设置</source><translation>Settings</translation></message>
        <message><source>仅本地处理</source><translation>Local processing only</translation></message>
        <message><source>仅发送检索片段</source><translation>Send retrieved excerpts only</translation></message>
        <message><source>发送选定页面</source><translation>Send selected pages</translation></message>
        <message><source>未知</source><translation>Unknown</translation></message>
        <message>
            <source>安装语言包…</source>
            <translation>Install Language Pack…</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::MainWindow</name>
        <message><source>查重</source><translation>Similarity check</translation></message>
        <message><source>开始录制</source><translation>Start recording</translation></message>
        <message><source>录制 PDF 演示或应用窗口、创建持续声音批注，并集中查看输出文件。</source><translation>Record a PDF presentation or app window, create continuous audio annotations, and manage outputs in one place.</translation></message>
        <message><source>录制与捕获</source><translation>Record and capture</translation></message>
        <message><source>录制中心</source><translation>Recording center</translation></message>
        <message><source>录制文件夹</source><translation>Recording folder</translation></message>
        <message><source>录制面板已打开；按 Win+Alt+R 开始或停止，视频保存在“视频\捕获”。</source><translation>The recording panel is open. Press Win+Alt+R to start or stop. Videos are saved under Videos\Captures.</translation></message>
        <message><source>打开录制文件夹</source><translation>Open recording folder</translation></message>
        <message><source>捕获</source><translation>Capture</translation></message>
        <message><source>无法打开文件夹：%1</source><translation>Could not open folder: %1</translation></message>
        <message><source>暂无截图或视频输出</source><translation>No screenshots or video outputs yet</translation></message>
        <message><source>持续录音</source><translation>Continuous audio recording</translation></message>
        <message><source>管理</source><translation>Manage</translation></message>
        <message><source>最近输出</source><translation>Recent outputs</translation></message>
    </context>
    <context>
        <name>aipdf::app::PdfView</name>
        <message><source>分析字符</source><translation>Characters analyzed</translation></message>
        <message><source>本地自查历史</source><translation>Local check history</translation></message>
        <message><source>未命名文档</source><translation>Untitled document</translation></message>
        <message><source>检查时间</source><translation>Check time</translation></message>
        <message><source>文档名称</source><translation>Document name</translation></message>
        <message><source>查重历史</source><translation>Check history</translation></message>
        <message><source>记录仅保存在本机，最多保留最近 30 次。</source><translation>Records are stored only on this device; the latest 30 are retained.</translation></message>
        <message><source>论文查重</source><translation>Thesis similarity check</translation></message>
        <message><source>论文查重历史</source><translation>Thesis check history</translation></message>
        <message><source>论文查重（本地自查）</source><translation>Thesis similarity check (local)</translation></message>
        <message><source>论文重复段落自查结果</source><translation>Local repeated-passage check results</translation></message>
        <message><source>还没有查重记录。</source><translation>There are no check records yet.</translation></message>
    </context>
    <context>
        <name>aipdf::app::ThesisCheckResult</name>
        <message><source>出现</source><translation>Occurrences:</translation></message>
        <message><source>参与分析的字符</source><translation>Characters analyzed</translation></message>
        <message><source>文内重复比例</source><translation>Internal repetition rate</translation></message>
        <message><source>未发现长度不少于 12 字符的重复句段。</source><translation>No repeated passage of at least 12 characters was found.</translation></message>
        <message><source>本地重复段落自查</source><translation>Local repeated-passage check</translation></message>
        <message><source>此结果只比较当前文档内部的完全重复句段，不连接论文数据库，也不能作为正式查重报告。</source><translation>This result compares only exact repeated passages within the current document. It does not use a thesis database and is not an official similarity report.</translation></message>
        <message><source>重复句段</source><translation>Repeated passages</translation></message>
        <message><source>重复字符</source><translation>Repeated characters</translation></message>
    </context>
    <context>
        <name>aipdf::app::MainWindow</name>
        <message>
            <source>编辑书签层级</source>
            <translation>Edit bookmark hierarchy</translation>
        </message>
        <message>
            <source>这份文档还没有书签。</source>
            <translation>This document has no bookmarks yet.</translation>
        </message>
        <message>
            <source>书签层级已更新</source>
            <translation>Bookmark hierarchy updated</translation>
        </message>
        <message><source>CSV 文件 (*.csv)</source><translation>CSV files (*.csv)</translation></message>
        <message><source>OFD 批量转换</source><translation>Batch OFD Conversion</translation></message>
        <message><source>OFD 文件 (*.ofd *.OFD)</source><translation>OFD files (*.ofd *.OFD)</translation></message>
        <message><source>保存报销单</source><translation>Save Reimbursement List</translation></message>
        <message><source>保存报销单失败。</source><translation>Failed to save the reimbursement list.</translation></message>
        <message><source>分类名称：</source><translation>Category name:</translation></message>
        <message><source>发票已归档：%1</source><translation>Invoice archived: %1</translation></message>
        <message><source>发票库中还没有已归档的 PDF。</source><translation>There are no archived PDFs in the invoice library.</translation></message>
        <message><source>发票归档</source><translation>Invoice Archive</translation></message>
        <message><source>发票管理</source><translation>Invoice Manager</translation></message>
        <message><source>可执行文件 (*.exe)</source><translation>Executables (*.exe)</translation></message>
        <message><source>否</source><translation>No</translation></message>
        <message><source>已取消星标。</source><translation>Star removed.</translation></message>
        <message><source>已添加星标。</source><translation>Star added.</translation></message>
        <message><source>已生成包含 %1 张发票的报销单。 </source><translation>Created a reimbursement list containing %1 invoices.</translation></message>
        <message><source>已转换 %1 个 OFD 文件。 </source><translation>Converted %1 OFD files.</translation></message>
        <message><source>已转换 %1 个，失败 %2 个。转换器应接受“输入文件 输出文件”参数。
失败：%3</source><translation>Converted %1 files; %2 failed. The converter must accept “input-file output-file” arguments.
Failed: %3</translation></message>
        <message><source>文件柜</source><translation>File Cabinet</translation></message>
        <message><source>文件：%1
页数：%2
大小：%3 KB
修改时间：%4
分类：%5
标签：%6
星标：%7</source><translation>File: %1
Pages: %2
Size: %3 KB
Modified: %4
Category: %5
Tags: %6
Starred: %7</translation></message>
        <message><source>文档分类</source><translation>Document Category</translation></message>
        <message><source>文档分类已保存。</source><translation>Document category saved.</translation></message>
        <message><source>文档标签</source><translation>Document Tags</translation></message>
        <message><source>文档标签已保存。</source><translation>Document tags saved.</translation></message>
        <message><source>文档概述</source><translation>Document Overview</translation></message>
        <message><source>无</source><translation>None</translation></message>
        <message><source>无法写入所选文件。</source><translation>Cannot write to the selected file.</translation></message>
        <message><source>无法复制发票到归档目录。</source><translation>Cannot copy the invoice to the archive directory.</translation></message>
        <message><source>无法打开发票归档目录。</source><translation>Cannot open the invoice archive directory.</translation></message>
        <message><source>无法打开目录：%1</source><translation>Cannot open directory: %1</translation></message>
        <message><source>是</source><translation>Yes</translation></message>
        <message><source>未分类</source><translation>Uncategorized</translation></message>
        <message><source>标签（用逗号分隔）：</source><translation>Tags (comma-separated):</translation></message>
        <message><source>正在监控自动导入目录：%1</source><translation>Monitoring auto-import directory: %1</translation></message>
        <message><source>生成报销单</source><translation>Create Reimbursement List</translation></message>
        <message><source>选择 OFD 文件</source><translation>Select OFD Files</translation></message>
        <message><source>选择 OFD 转 PDF 工具</source><translation>Select OFD-to-PDF Tool</translation></message>
        <message><source>选择自动导入目录</source><translation>Select Auto-import Directory</translation></message>
        <message><source>正在转换 OFD 文件…</source><translation>Converting OFD files…</translation></message>
        <message><source>取消</source><translation>Cancel</translation></message>
        <message><source>正在转换：%1</source><translation>Converting: %1</translation></message>
        <message><source>OFD 转换已取消；已完成 %1 个文件。</source><translation>OFD conversion canceled; %1 files completed.</translation></message>
        <message><source>关于</source><translation>About</translation></message>
    </context>
    <context>
        <name>aipdf::app::PdfView</name>
        <message><source>文内重复比例</source><translation>Internal repetition ratio</translation></message>
        <message><source>变换页面对象</source><translation>Transform page objects</translation></message>
        <message><source>已变换 %1 个页面对象</source><translation>Transformed %1 page object(s)</translation></message>
        <message><source>已撤销：%1</source><translation>Undone: %1</translation></message>
        <message><source>已重做：%1</source><translation>Redone: %1</translation></message>
        <message><source>撤销 %1 (Ctrl+Z)</source><translation>Undo %1 (Ctrl+Z)</translation></message>
        <message><source>段落重排</source><translation>paragraph reflow</translation></message>
        <message><source>行内自动换行</source><translation>inline wrapping</translation></message>
        <message><source>重做 %1 (Ctrl+Shift+Z)</source><translation>Redo %1 (Ctrl+Shift+Z)</translation></message>
        <message><source>X 坐标（点）:</source><translation>X coordinate (pt):</translation></message>
        <message><source>Y 坐标（点）:</source><translation>Y coordinate (pt):</translation></message>
        <message><source>已旋转 %1 个页面对象</source><translation>Rotated %1 page object(s)</translation></message>
        <message><source>已选择 %1 个对象</source><translation>Selected %1 object(s)</translation></message>
        <message><source>已选择 %1 个对象；再次单击可切换下层对象</source><translation>Selected %1 object(s); click again to cycle through objects underneath</translation></message>
        <message><source>当前位置没有页面对象</source><translation>There is no page object at this position</translation></message>
        <message><source>旋转</source><translation>Rotate</translation></message>
        <message><source>旋转对象</source><translation>Rotate objects</translation></message>
        <message><source>旋转角度（度）:</source><translation>Rotation angle (degrees):</translation></message>
        <message><source>旋转页面对象</source><translation>Rotate page objects</translation></message>
        <message><source>精确几何</source><translation>Precise geometry</translation></message>
        <message><source>上移一层</source><translation>Bring Forward</translation></message>
        <message><source>下移一层</source><translation>Send Backward</translation></message>
        <message><source>书写方向:</source><translation>Writing direction:</translation></message>
        <message><source>双击位置没有可编辑文字</source><translation>There is no editable text at the double-clicked position</translation></message>
        <message><source>取消组合</source><translation>Ungroup</translation></message>
        <message><source>右侧裁剪（%）:</source><translation>Crop right (%):</translation></message>
        <message><source>图片文件 (*.png *.jpg *.jpeg *.bmp *.webp)</source><translation>Image files (*.png *.jpg *.jpeg *.bmp *.webp)</translation></message>
        <message><source>基线偏移（点）:</source><translation>Baseline shift (pt):</translation></message>
        <message><source>字距（点）:</source><translation>Character spacing (pt):</translation></message>
        <message><source>安全内容流重写失败：%1</source><translation>Safe content-stream rewrite failed: %1</translation></message>
        <message><source>安全搜索替换</source><translation>safe search and replace</translation></message>
        <message><source>就地编辑文字</source><translation>inline text editing</translation></message>
        <message><source>左侧裁剪（%）:</source><translation>Crop left (%):</translation></message>
        <message><source>已安全替换 %1 处</source><translation>Safely replaced %1 occurrence(s)</translation></message>
        <message><source>底部裁剪（%）:</source><translation>Crop bottom (%):</translation></message>
        <message><source>当前字体不包含输入字符，文字没有修改。</source><translation>The current font cannot represent the entered characters. The text was not changed.</translation></message>
        <message><source>文字已更新</source><translation>Text updated</translation></message>
        <message><source>文本框空间不足，仍有文字未排入</source><translation>The text frames are full; some text could not be placed</translation></message>
        <message><source>替换图片对象</source><translation>Replace image object</translation></message>
        <message><source>横排</source><translation>Horizontal</translation></message>
        <message><source>组合</source><translation>Group</translation></message>
        <message><source>解锁</source><translation>Unlock</translation></message>
        <message><source>编辑文字</source><translation>Edit text</translation></message>
        <message><source>置于底层</source><translation>Send to Back</translation></message>
        <message><source>置于顶层</source><translation>Bring to Front</translation></message>
        <message><source>行距（倍）:</source><translation>Line spacing (multiple):</translation></message>
        <message><source>裁剪图片对象</source><translation>Crop image object</translation></message>
        <message><source>竖排</source><translation>Vertical</translation></message>
        <message><source>选区包含锁定对象，请先解锁</source><translation>The selection contains locked objects. Unlock them first.</translation></message>
        <message><source>选择替换图片</source><translation>Select replacement image</translation></message>
        <message><source>透明度</source><translation>Opacity</translation></message>
        <message><source>透明度（%）:</source><translation>Opacity (%):</translation></message>
        <message><source>锁定</source><translation>Lock</translation></message>
        <message><source>顶部裁剪（%）:</source><translation>Crop top (%):</translation></message>
        <message><source>安全替换选中文字</source><translation>safely replace selected text</translation></message>
        <message><source>已安全替换选中文字</source><translation>Selected text was safely replaced</translation></message>
        <message><source>替换选中文字</source><translation>replace selected text</translation></message>
        <message><source>文字对象已锁定，请先解锁</source><translation>The text object is locked. Unlock it first.</translation></message>
    </context>
    <context>
        <name>aipdf::app::FormCalculationDialog</name>
        <message>
            <source>计算次序</source>
            <translation>Calculation order</translation>
        </message>
        <message>
            <source>顺序</source>
            <translation>Order</translation>
        </message>
        <message>
            <source>字段</source>
            <translation>Field</translation>
        </message>
        <message>
            <source>计算脚本</source>
            <translation>Calculation script</translation>
        </message>
        <message>
            <source>执行结果</source>
            <translation>Result</translation>
        </message>
        <message>
            <source>执行全部</source>
            <translation>Run all</translation>
        </message>
        <message>
            <source>上移</source>
            <translation>Move up</translation>
        </message>
        <message>
            <source>下移</source>
            <translation>Move down</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::FormScriptDialog</name>
        <message>
            <source>运行表单脚本</source>
            <translation>Run form script</translation>
        </message>
        <message>
            <source>结果写入字段</source>
            <translation>Write the result into field</translation>
        </message>
        <message>
            <source>（只算不写回）</source>
            <translation>(Calculate only, do not write back)</translation>
        </message>
        <message>
            <source>脚本</source>
            <translation>Script</translation>
        </message>
        <message>
            <source>运行</source>
            <translation>Run</translation>
        </message>
        <message>
            <source>运行结果会显示在这里。</source>
            <translation>The result will appear here.</translation>
        </message>
        <message>
            <source>先写一段脚本再运行。</source>
            <translation>Write a script first, then run it.</translation>
        </message>
        <message>
            <source>写一段脚本，例如：
AFNumber_Format(2, event.value, 0, 0, ".", false)</source>
            <translation>Write a script, for example:
AFNumber_Format(2, event.value, 0, 0, ".", false)</translation>
        </message>
        <message>
            <source>结果：%1</source>
            <translation>Result: %1</translation>
        </message>
        <message>
            <source>提示：%1</source>
            <translation>Note: %1</translation>
        </message>
        <message>
            <source>脚本没跑通：%1</source>
            <translation>The script did not run: %1</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::FormDesignAssistantDialog</name>
        <message>
            <source>表单设计助手</source>
            <translation>Form design assistant</translation>
        </message>
        <message>
            <source>创建字段</source>
            <translation>Create fields</translation>
        </message>
        <message>
            <source>页</source>
            <translation>Page</translation>
        </message>
        <message>
            <source>左</source>
            <translation>Left</translation>
        </message>
        <message>
            <source>上</source>
            <translation>Top</translation>
        </message>
        <message>
            <source>宽</source>
            <translation>Width</translation>
        </message>
        <message>
            <source>高</source>
            <translation>Height</translation>
        </message>
        <message>
            <source>模型给出了 %1 个字段，要铺 %2 页（还差 %3 页），改完再创建。</source>
            <translation>The model gave %1 fields; they need %2 pages (%3 more needed). Review them and create.</translation>
        </message>
        <message>
            <source>识别出 %1 个候选字段，改完再创建。</source>
            <translation>Detected %1 candidate fields. Review them and create.</translation>
        </message>
        <message>
            <source>没有字段可以排布。</source>
            <translation>There are no fields to lay out.</translation>
        </message>
        <message>
            <source>拿不到页面尺寸，排不了版面。</source>
            <translation>Cannot lay out the form: the page size is unavailable.</translation>
        </message>
        <message>
            <source>生成设计</source>
            <translation>Generate design</translation>
        </message>
        <message>
            <source>在当前页创建</source>
            <translation>Create on current page</translation>
        </message>
        <message>
            <source>模型原始回答</source>
            <translation>Raw model answer</translation>
        </message>
        <message>
            <source>字段名</source>
            <translation>Field name</translation>
        </message>
        <message>
            <source>提示</source>
            <translation>Tooltip</translation>
        </message>
        <message>
            <source>类型</source>
            <translation>Type</translation>
        </message>
        <message>
            <source>左 %</source>
            <translation>Left %</translation>
        </message>
        <message>
            <source>上 %</source>
            <translation>Top %</translation>
        </message>
        <message>
            <source>宽 %</source>
            <translation>Width %</translation>
        </message>
        <message>
            <source>高 %</source>
            <translation>Height %</translation>
        </message>
        <message>
            <source>写一句描述，例如：一张增值税发票，需要购买方名称、纳税人识别号、开票日期、金额、税率。</source>
            <translation>Describe the form, for example: a VAT invoice with buyer name, tax ID, invoice date, amount and tax rate.</translation>
        </message>
        <message>
            <source>模型正在设计…</source>
            <translation>The model is designing…</translation>
        </message>
        <message>
            <source>模型给出了 %1 个字段，改完再创建。</source>
            <translation>The model proposed %1 fields; review them, then create.</translation>
        </message>
        <message>
            <source>模型没有返回可用的 JSON 字段清单。</source>
            <translation>The model did not return a usable JSON field list.</translation>
        </message>
        <message>
            <source>模型没有给出任何字段。</source>
            <translation>The model did not propose any fields.</translation>
        </message>
        <message>
            <source>模型返回的字段清单解析失败：%1</source>
            <translation>Failed to parse the field list returned by the model: %1</translation>
        </message>
        <message>
            <source>模型调用失败：%1</source>
            <translation>Model call failed: %1</translation>
        </message>
        <message>
            <source>AI 服务调用失败：%1</source>
            <translation>AI service call failed: %1</translation>
        </message>
        <message>
            <source>AI 服务还没连上。</source>
            <translation>The AI service is not connected yet.</translation>
        </message>
        <message>
            <source>先写一句描述。</source>
            <translation>Write a description first.</translation>
        </message>
        <message>
            <source>（已停止）</source>
            <translation>(stopped)</translation>
        </message>
        <message>
            <source>表单设计描述</source>
            <translation>Form design description</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::FormToolSettingsDialog</name>
        <message>
            <source>表单工具设置</source>
            <translation>Form tool settings</translation>
        </message>
        <message>
            <source>默认字体</source>
            <translation>Default font</translation>
        </message>
        <message>
            <source>默认字号</source>
            <translation>Default font size</translation>
        </message>
        <message>
            <source>文字颜色</source>
            <translation>Text color</translation>
        </message>
        <message>
            <source>选择文字颜色</source>
            <translation>Choose text color</translation>
        </message>
    </context>
<context>
    <name>aipdf::pdf::TagTreeWriter</name>
        <message>
            <source>PDF 的根对象不是字典</source>
            <translation>The PDF root object is not a dictionary</translation>
        </message>
        <message>
            <source>PDF 里没有页面</source>
            <translation>The PDF has no pages</translation>
        </message>
        <message>
            <source>qpdf 写出的 PDF 为空</source>
            <translation>The PDF written by qpdf is empty</translation>
        </message>
        <message>
            <source>qpdf 没有给出细节</source>
            <translation>qpdf gave no details</translation>
        </message>
                <message>
            <source>内容流无法解析：%1</source>
            <translation>The content stream cannot be parsed: %1</translation>
        </message>
<message>
            <source>写出的 PDF 无法重新打开：%1</source>
            <translation>The written PDF cannot be reopened: %1</translation>
        </message>
        <message>
            <source>写出的 PDF 结构有问题：%1</source>
            <translation>The written PDF has structural problems: %1</translation>
        </message>
        <message>
            <source>当前构建没有启用 qpdf，无法写入标签树</source>
            <translation>This build does not have qpdf enabled, so the tag tree cannot be written</translation>
        </message>
                <message>
            <source>当前构建没有启用 qpdf，无法扫描内容流</source>
            <translation>This build has no qpdf, so the content stream cannot be scanned</translation>
        </message>
<message>
            <source>当前构建没有启用 qpdf，无法读取标签树</source>
            <translation>This build does not have qpdf enabled, so the tag tree cannot be read</translation>
        </message>
                <message>
            <source>标签树计划里没有结构元素</source>
            <translation>The tag tree plan has no structure elements</translation>
        </message>
<message>
            <source>第 %1 页的内容流无法解析：%2</source>
            <translation>The content stream of page %1 cannot be parsed: %2</translation>
        </message>
        <message>
            <source>解析 PDF 失败：%1</source>
            <translation>Parsing the PDF failed: %1</translation>
        </message>
        <message>
            <source>输入的 PDF 内容为空</source>
            <translation>The input PDF is empty</translation>
        </message>
</context>
<context>
    <name>aipdf::app::LastWaveBridge</name>
    <message><source>
%1</source><translation>
%1</translation></message>
    <message><source>
另外有 %1 段里的字符标准 14 字体显示不了，已写成占位符。</source><translation>
In addition, %1 paragraph(s) contain characters the standard 14 fonts cannot show; they were written as placeholders.</translation></message>
    <message><source>
提醒：%1</source><translation>
Warning: %1</translation></message>
    <message><source>
有 %1 项内容没有搬过去（见下方清单）。</source><translation>
%1 item(s) were not carried over (see the list below).</translation></message>
    <message><source>
说明：%1</source><translation>
Notes: %1</translation></message>
    <message><source> %1</source><translation> %1</translation></message>
    <message><source> [%1]</source><translation> [%1]</translation></message>
    <message><source> 值：%1</source><translation> value: %1</translation></message>
    <message><source> 实际文字：%1</source><translation> Actual text: %1</translation></message>
    <message><source> 建议：%1</source><translation> Suggestion: %1</translation></message>
    <message><source> 必填</source><translation> required</translation></message>
    <message><source> 替代文本：%1</source><translation> Alternative text: %1</translation></message>
    <message><source> 正文：%1</source><translation> Body text: %1</translation></message>
    <message><source> 用途：%1</source><translation> Intent: %1</translation></message>
    <message><source> 语言：%1</source><translation> Language: %1</translation></message>
    <message><source> 顺序：%1</source><translation> Order: %1</translation></message>
    <message><source>%</source><translation>%</translation></message>
            <message>
            <source>%1 MB</source>
            <translation>%1 MB</translation>
        </message>
        <message>
            <source>%1 KB</source>
            <translation>%1 KB</translation>
        </message>
<message><source>%1 倍</source><translation>%1x</translation></message>
            <message>
            <source>%1 字节</source>
            <translation>%1 bytes</translation>
        </message>
<message><source>-</source><translation>-</translation></message>
            <message>
            <source>3D 注解：只写对结构，**不渲染**（本程序没有 3D 运行时，模型不会转起来）；多媒体注解：同样只写结构，但**音视频文件可以在下面的预览里解码并播放**（Windows Media Foundation + 系统声卡）；写进文档的注解要靠带相应运行时的阅读器才放得出来。</source>
            <translation>3D annotations: only the structure is written, **nothing is rendered** (this program has no 3D runtime, so the model does not spin); multimedia annotations likewise only carry the structure, but **audio and video files can be decoded and played in the preview below** (Windows Media Foundation + the system sound card); an annotation written into the document can only be played back by a reader that has the matching runtime.</translation>
        </message>
<message><source>[%1] 第 %2 页：%3</source><translation>[%1] page %2: %3</translation></message>
    <message><source>两端对齐</source><translation>Justify</translation></message>
    <message><source>共 %1 个图层。</source><translation>%1 layer(s) in total.</translation></message>
    <message><source>共 %1 步：执行 %2 步，成功 %3 步，失败 %4 步，因前一步失败没执行 %5 步。</source><translation>%1 step(s) in total: %2 executed, %3 succeeded, %4 failed, %5 not executed because an earlier step failed.</translation></message>
    <message><source>写入不可见文字层失败（领域层没有给出原因）。</source><translation>Writing the invisible text layer failed (the domain layer gave no reason).</translation></message>
    <message><source>写回是按上表的图层定义整体重写 /OCProperties：原来标到图层上的内容流标记不会被重建。</source><translation>Writing back rewrites /OCProperties as a whole from the layer list above: content stream markers that used to point at layers are not rebuilt.</translation></message>
    <message><source>动作向导只编排本程序已有的文档级操作（合并、加页码、加水印、加密、转换格式等），不是脚本引擎，也不会执行文档里的脚本。</source><translation>The action wizard only sequences document-level operations this program already has (merge, page numbers, watermark, encryption, format conversion and so on); it is not a script engine and never runs scripts from the document.</translation></message>
            <message>
            <source>只嵌了用到的字形（%1，原字体 %2），文件不会撑大。</source>
            <translation>Only the glyphs actually used are embedded (%1, original font %2), so the file stays small.</translation>
        </message>
<message><source>右对齐</source><translation>Right</translation></message>
    <message><source>固定 %1 点</source><translation>fixed %1 pt</translation></message>
    <message><source>字号 %1 点；行距 %2；%3；首行缩进 %4 点；左缩进 %5 点；右缩进 %6 点。</source><translation>Font size %1 pt; line spacing %2; %3; first line indent %4 pt; left indent %5 pt; right indent %6 pt.</translation></message>
    <message><source>居中</source><translation>Centre</translation></message>
    <message><source>左对齐</source><translation>Left</translation></message>
    <message><source>已写入不可见文字层，现在可以搜索/复制这页文字了。共 %1 页、%2 个字符。</source><translation>The invisible text layer was written; this page can now be searched and copied. %1 page(s), %2 character(s).</translation></message>
            <message>
            <source>已嵌入字体，中文按嵌入字形显示。</source>
            <translation>A font is embedded; Chinese is drawn with the embedded glyphs.</translation>
        </message>
        <message>
            <source>已嵌入字体 %1 来显示中文。</source>
            <translation>Font %1 is embedded to display Chinese.</translation>
        </message>
<message><source>已生成 %1 页。</source><translation>%1 page(s) generated.</translation></message>
    <message><source>文档语言：%1。</source><translation>Document language: %1.</translation></message>
    <message><source>文档语言：文件里没有写。</source><translation>Document language: not written in the file.</translation></message>
    <message><source>有 %1 个字符无法显示，已替换（默认用标准 14 字体，它没有中文字形；要正确显示中文请用「选择字体文件」选一份 TTF）。</source><translation>%1 character(s) cannot be shown and were replaced (the standard 14 fonts have no Chinese glyphs; use &quot;Select a font file&quot; to pick a TTF so Chinese shows correctly).</translation></message>
    <message><source>本地索引里没找到相关内容。</source><translation>Nothing related was found in the local index.</translation></message>
    <message><source>没有地方可以放转换结果。</source><translation>There is nowhere to put the conversion result.</translation></message>
    <message><source>沿用发票上的税率</source><translation>keep the tax rate on the invoice</translation></message>
    <message><source>用标准 14 字体排版（没有嵌入字体）。</source><translation>Laid out with the standard 14 fonts (no embedded font).</translation></message>
    <message><source>第 %1 步（%2）失败：%3</source><translation>Step %1 (%2) failed: %3</translation></message>
    <message><source>第 %1 页：%2</source><translation>Page %1: %2</translation></message>
    <message><source>第 %1 页：%2（涉及图层：%3）</source><translation>Page %1: %2 (layers involved: %3)</translation></message>
    <message><source>结论：不合规（通过 %1 项、警告 %2 项、不通过 %3 项）。</source><translation>Verdict: not compliant (%1 passed, %2 warnings, %3 failed).</translation></message>
    <message><source>结论：合规（通过 %1 项、警告 %2 项、不通过 %3 项）。</source><translation>Verdict: compliant (%1 passed, %2 warnings, %3 failed).</translation></message>
    <message><source>识别结果有 %1 页，文档有 %2 页（相差 %3 页）。</source><translation>The recognition result has %1 page(s) but the document has %2 (a difference of %3 page(s)).</translation></message>
    <message><source>读到 %1 个 XFA 字段（跳过 %2 个没有名字的）。</source><translation>%1 XFA field(s) read (%2 without a name were skipped).</translation></message>
    <message><source>读到标签树：%1 个结构元素。</source><translation>Tag tree read: %1 structure element(s).</translation></message>
    <message><source>读回：%1 个 3D 注解、%2 个多媒体注解。</source><translation>Read back: %1 3D annotation(s), %2 media annotation(s).</translation></message>
    <message><source>这一步没有执行。</source><translation>This step was not executed.</translation></message>
    <message><source>这一页的识别被取消了。</source><translation>Recognition of this page was cancelled.</translation></message>
    <message><source>这个检查器只做检查并给出修复建议，不会生成 PDF/UA 合规文件。</source><translation>This checker only checks and suggests fixes; it does not produce a PDF/UA compliant file.</translation></message>
    <message><source>这份 XFA 含动态逻辑（%1）：本程序只做静态字段提取，不执行脚本，表单在阅读器里的实际行为取决于那些脚本。</source><translation>This XFA contains dynamic logic (%1): this program only extracts static fields and does not run scripts; how the form behaves in a reader depends on those scripts.</translation></message>
    <message><source>这份文档不是 XFA 表单（没有 /XFA 数据）。</source><translation>This document is not an XFA form (no /XFA data).</translation></message>
    <message><source>这份文档没有图层（没有 /OCProperties）。</source><translation>This document has no layers (no /OCProperties).</translation></message>
    <message><source>这份文档没有标签树（文件里没有 /StructTreeRoot）。</source><translation>This document has no tag tree (no /StructTreeRoot in the file).</translation></message>
            <message>
            <source>重排会改变版面：页数会变，图表与表格的位置会变，页眉页脚会按每页重复放到每一页，图形、表格与公式按原位置搬过去、不重排。</source>
            <translation>Reflowing changes the layout: the page count changes, figures and tables move, headers and footers are repeated on every page, and graphics, tables and formulas are moved to their original positions without being reflowed.</translation>
        </message>
<message><source>重排完成：原来 %1 页，现在 %2 页；重排了 %3 段正文。</source><translation>Reflow done: %1 page(s) before, %2 page(s) now; %3 body block(s) reflowed.</translation></message>
    <message><source>重新识别会替换上一次写入的隐藏文字层（不是再叠一层）：两层叠在同一坐标上时，阅读器只会保留其中一层。</source><translation>Recognising again replaces the hidden text layer written last time (it is not stacked): with two layers at the same coordinates a reader keeps only one of them.</translation></message>
    <message><source>（%1）</source><translation>(%1)</translation></message>
    <message><source>（原第 %1 页）</source><translation>(page %1 of the original)</translation></message>
    <message><source>（层级 %1）</source><translation>(depth %1)</translation></message>
    <message><source>（无标签）</source><translation>(no tag)</translation></message>
    <message><source>（第 %1 页，层级 %2）</source><translation>(page %1, depth %2)</translation></message>
    <message><source>（默认可见）</source><translation>(visible by default)</translation></message>
    <message><source>（默认隐藏）</source><translation>(hidden by default)</translation></message>
    <message><source>：%1</source><translation>: %1</translation></message>
</context>

<context>
    <name>aipdf::app::LastWavePanels</name>
    <message><source>0 位</source><translation>0 digits</translation></message>
    <message><source>2 位</source><translation>2 digits</translation></message>
    <message><source>3 位</source><translation>3 digits</translation></message>
    <message><source>3D 模型（U3D）</source><translation>3D model (U3D)</translation></message>
    <message><source>CSV 文件 (*.csv)</source><translation>CSV files (*.csv)</translation></message>
    <message><source>PDF/UA 合规检查</source><translation>PDF/UA compliance check</translation></message>
    <message><source>X</source><translation>X</translation></message>
    <message><source>XFA 字段</source><translation>XFA fields</translation></message>
    <message><source>XFA 字段列表</source><translation>XFA field list</translation></message>
    <message><source>XFA 摘要</source><translation>XFA summary</translation></message>
    <message><source>Y</source><translation>Y</translation></message>
    <message><source>上移</source><translation>Move up</translation></message>
    <message><source>下移</source><translation>Move down</translation></message>
    <message><source>不通过</source><translation>Failed</translation></message>
    <message><source>从文件新建 PDF</source><translation>New PDF from a file</translation></message>
    <message><source>保存为 PDF…</source><translation>Save as PDF…</translation></message>
    <message><source>倍数</source><translation>Multiple</translation></message>
    <message><source>值</source><translation>Value</translation></message>
    <message><source>先选一个文件。</source><translation>Choose a file first.</translation></message>
    <message><source>先选一个源文件。</source><translation>Choose a source file first.</translation></message>
    <message><source>写不了这个文件：%1</source><translation>Cannot write this file: %1</translation></message>
    <message><source>写入结果</source><translation>Write result</translation></message>
    <message><source>写回文档</source><translation>Write back to the document</translation></message>
    <message><source>写回说明</source><translation>Write-back notice</translation></message>
    <message><source>删除一行</source><translation>Delete row</translation></message>
    <message><source>删除这一步</source><translation>Delete this step</translation></message>
    <message><source>动作向导</source><translation>Action wizard</translation></message>
    <message><source>动作类型</source><translation>Action type</translation></message>
    <message><source>动态 XFA 提示</source><translation>Dynamic XFA notice</translation></message>
    <message><source>单价</source><translation>Unit price</translation></message>
    <message><source>单位</source><translation>Unit</translation></message>
    <message><source>参数名</source><translation>Parameter name</translation></message>
    <message><source>发 票</source><translation>INVOICE</translation></message>
    <message><source>发票号码</source><translation>Invoice number</translation></message>
    <message><source>发票打印</source><translation>Invoice printing</translation></message>
    <message><source>发票要素不完整。</source><translation>The invoice is incomplete.</translation></message>
    <message><source>右缩进</source><translation>Right indent</translation></message>
    <message><source>右缩进（点）</source><translation>Right indent (points)</translation></message>
    <message><source>合规结论</source><translation>Compliance conclusion</translation></message>
    <message><source>合计 %1；税额 %2；价税合计 %3</source><translation>Subtotal %1; tax %2; grand total %3</translation></message>
    <message><source>名称</source><translation>Name</translation></message>
    <message><source>向导名</source><translation>Recipe name</translation></message>
    <message><source>否</source><translation>No</translation></message>
    <message><source>固定磅值</source><translation>Fixed points</translation></message>
    <message><source>图层列表</source><translation>Layer list</translation></message>
    <message><source>图层摘要</source><translation>Layer summary</translation></message>
    <message><source>图层面板</source><translation>Layer panel</translation></message>
    <message><source>多媒体（音频 / 视频 / 图片）</source><translation>Media (audio / video / image)</translation></message>
    <message><source>字体文件</source><translation>Font file</translation></message>
    <message><source>字体文件 (*.ttf *.ttc)</source><translation>Font files (*.ttf *.ttc)</translation></message>
    <message><source>字体文件（可选，TTF）</source><translation>Font file (optional, TTF)</translation></message>
    <message><source>字号</source><translation>Font size</translation></message>
    <message><source>字号（点）</source><translation>Font size (points)</translation></message>
    <message><source>字符串</source><translation>String</translation></message>
    <message><source>字符串列表</source><translation>String list</translation></message>
    <message><source>实际文字</source><translation>Actual text</translation></message>
    <message><source>宽</source><translation>Width</translation></message>
    <message><source>对齐</source><translation>Alignment</translation></message>
    <message><source>导出 PDF</source><translation>Export PDF</translation></message>
    <message><source>导出 XFA 字段</source><translation>Export XFA fields</translation></message>
    <message><source>导出字段 CSV</source><translation>Export fields as CSV</translation></message>
    <message><source>小数</source><translation>Decimal</translation></message>
    <message><source>小数位</source><translation>Decimal places</translation></message>
    <message><source>层级</source><translation>Depth</translation></message>
    <message><source>左缩进</source><translation>Left indent</translation></message>
    <message><source>左缩进（点）</source><translation>Left indent (points)</translation></message>
    <message><source>币种</source><translation>Currency</translation></message>
    <message><source>布尔</source><translation>Boolean</translation></message>
    <message><source>建议</source><translation>Suggestion</translation></message>
    <message><source>开票日期</source><translation>Issue date</translation></message>
    <message><source>当前值</source><translation>Current value</translation></message>
    <message><source>必填</source><translation>Required</translation></message>
    <message><source>我知道重排会改变版面（页数、图表位置都会变）</source><translation>I understand that reflowing changes the layout (page count and figure positions change)</translation></message>
    <message><source>打印</source><translation>Print</translation></message>
    <message><source>执行结果</source><translation>Run result</translation></message>
    <message><source>抬头</source><translation>Title</translation></message>
    <message><source>按新页宽重排</source><translation>Reflow to a new page width</translation></message>
    <message><source>按页宽自动算</source><translation>Computed from the page width</translation></message>
    <message><source>排成 %1 行%s</source><translation>Laid out as %1 line(s)%s</translation></message>
    <message><source>排版</source><translation>Lay out</translation></message>
    <message><source>排版失败：%1</source><translation>Layout failed: %1</translation></message>
    <message><source>排版结果</source><translation>Layout result</translation></message>
    <message><source>插入</source><translation>Insert</translation></message>
    <message><source>插入 3D 模型 / 多媒体</source><translation>Insert 3D model / media</translation></message>
    <message><source>数量</source><translation>Quantity</translation></message>
    <message><source>整数</source><translation>Integer</translation></message>
    <message><source>文件</source><translation>File</translation></message>
    <message><source>文件路径</source><translation>File path</translation></message>
    <message><source>文本文件 (*.txt *.md *.csv *.json *.html *.htm)</source><translation>Text files (*.txt *.md *.csv *.json *.html *.htm)</translation></message>
    <message><source>文档标签树</source><translation>Document tag tree</translation></message>
    <message><source>新页宽</source><translation>New page width</translation></message>
    <message><source>新页宽（点）</source><translation>New page width (points)</translation></message>
    <message><source>新页高</source><translation>New page height</translation></message>
    <message><source>新页高（点，0 = 自动）</source><translation>New page height (points, 0 = automatic)</translation></message>
    <message><source>是</source><translation>Yes</translation></message>
    <message><source>替代文本</source><translation>Alternative text</translation></message>
    <message><source>标签</source><translation>Label</translation></message>
    <message><source>标签树元素</source><translation>Tag tree elements</translation></message>
    <message><source>标签树摘要</source><translation>Tag tree summary</translation></message>
    <message><source>标题</source><translation>Title</translation></message>
    <message><source>校验没通过：%1</source><translation>Validation failed: %1</translation></message>
    <message><source>校验结果</source><translation>Validation result</translation></message>
    <message><source>检查器能力说明</source><translation>Checker scope</translation></message>
    <message><source>正文</source><translation>Body text</translation></message>
    <message><source>步骤列表</source><translation>Step list</translation></message>
    <message><source>步骤参数</source><translation>Step parameters</translation></message>
    <message><source>段落</source><translation>Paragraph</translation></message>
    <message><source>段落参数</source><translation>Paragraph parameters</translation></message>
    <message><source>段落排版</source><translation>Paragraph formatting</translation></message>
    <message><source>没有搬过去的内容</source><translation>Content that was not carried over</translation></message>
    <message><source>没有页面级覆盖：所有页共用同一套图层可见性。</source><translation>No page-level overrides: every page uses the same layer visibility.</translation></message>
            <message>
            <source>没选字体时：内容里有中文就自动用系统里的中文字体，纯西文用标准 14 字体。</source>
            <translation>When no font is chosen: Chinese text automatically uses a system CJK font, Western-only text uses the standard 14 fonts.</translation>
        </message>
<message><source>注解类型</source><translation>Annotation type</translation></message>
    <message><source>添加一行</source><translation>Add row</translation></message>
    <message><source>添加这一步</source><translation>Add this step</translation></message>
    <message><source>源文件</source><translation>Source file</translation></message>
    <message><source>源文件（txt / md / csv / json / html）</source><translation>Source file (txt / md / csv / json / html)</translation></message>
    <message><source>版式预览</source><translation>Layout preview</translation></message>
    <message><source>版面变化说明</source><translation>Layout change notice</translation></message>
    <message><source>目标框（PDF 用户空间，左下原点）</source><translation>Target box (PDF user space, origin at the bottom left)</translation></message>
    <message><source>码</source><translation>Code</translation></message>
    <message><source>确认版面会变</source><translation>Confirm the layout changes</translation></message>
    <message><source>税率</source><translation>Tax rate</translation></message>
    <message><source>税率（%）</source><translation>Tax rate (%)</translation></message>
    <message><source>类型</source><translation>Type</translation></message>
    <message><source>给这份编排起个名字</source><translation>Name this recipe</translation></message>
    <message><source>能力说明</source><translation>Scope</translation></message>
    <message><source>行距</source><translation>Line spacing</translation></message>
    <message><source>行距口径</source><translation>Line spacing mode</translation></message>
    <message><source>行距（倍）</source><translation>Line spacing (multiple)</translation></message>
    <message><source>行项目</source><translation>Line items</translation></message>
    <message><source>要排的文字</source><translation>Text to lay out</translation></message>
    <message><source>规格</source><translation>Specification</translation></message>
    <message><source>警告</source><translation>Warning</translation></message>
    <message><source>语言</source><translation>Language</translation></message>
    <message><source>说明</source><translation>Detail</translation></message>
    <message><source>读不了这个字体文件：%1</source><translation>Cannot read this font file: %1</translation></message>
    <message><source>读不了这个文件：%1</source><translation>Cannot read this file: %1</translation></message>
    <message><source>购买方名称</source><translation>Buyer name</translation></message>
    <message><source>购买方税号</source><translation>Buyer tax ID</translation></message>
    <message><source>转换</source><translation>Convert</translation></message>
    <message><source>转换失败。</source><translation>Conversion failed.</translation></message>
    <message><source>转换结果说明</source><translation>Conversion result notice</translation></message>
    <message><source>运行</source><translation>Run</translation></message>
    <message><source>这个字体文件是空的：%1</source><translation>This font file is empty: %1</translation></message>
    <message><source>这份 XFA 没有动态逻辑：字段都是静态的。</source><translation>This XFA has no dynamic logic: every field is static.</translation></message>
    <message><source>选择字体文件</source><translation>Select a font file</translation></message>
    <message><source>选择字体文件…</source><translation>Select a font file…</translation></message>
    <message><source>选择文件</source><translation>Select a file</translation></message>
    <message><source>选择文件…</source><translation>Select a file…</translation></message>
    <message><source>选择源文件</source><translation>Select a source file</translation></message>
    <message><source>通过</source><translation>Passed</translation></message>
    <message><source>重排</source><translation>Reflow</translation></message>
    <message><source>重排会改变版面：请先勾选「我知道重排会改变版面」。</source><translation>Reflowing changes the layout: tick &quot;I understand that reflowing changes the layout&quot; first.</translation></message>
    <message><source>重排结果</source><translation>Reflow result</translation></message>
    <message><source>销售方名称</source><translation>Seller name</translation></message>
    <message><source>销售方税号</source><translation>Seller tax ID</translation></message>
    <message><source>页码</source><translation>Page</translation></message>
    <message><source>页面级覆盖</source><translation>Page-level overrides</translation></message>
    <message><source>页面级覆盖：
%1</source><translation>Page-level overrides:
%1</translation></message>
            <message>
            <source>预览</source>
            <translation>Preview</translation>
        </message>
<message><source>首行缩进</source><translation>First line indent</translation></message>
    <message><source>首行缩进（点）</source><translation>First line indent (points)</translation></message>
    <message><source>高</source><translation>Height</translation></message>
    <message><source>（没有内容被丢掉）</source><translation>(Nothing was dropped)</translation></message>
    <message><source>，有文字没排进去</source><translation>, some text did not fit</translation></message>
</context>

<context>
    <name>aipdf::app::LastWaveWriter</name>
    <message><source>qpdf 写入失败：%1</source><translation>qpdf failed to write: %1</translation></message>
    <message><source>qpdf 没有写出内容。</source><translation>qpdf wrote nothing.</translation></message>
    <message><source>内容流是空的，没有东西可写。</source><translation>The content stream is empty; there is nothing to write.</translation></message>
    <message><source>本次构建没有启用 qpdf，往页面追加内容流不可用。</source><translation>This build has no qpdf, so appending a content stream to a page is not available.</translation></message>
    <message><source>没有可写入的 PDF 内容。</source><translation>There is no PDF content to write.</translation></message>
    <message><source>页码超出范围：%1</source><translation>Page number out of range: %1</translation></message>
</context>
    <context>
        <name>aipdf::app::Language</name>
            <message>
                <source>没有找到 %1 的语言包（%2），界面仍然是中文。</source>
                <translation>No language pack for %1 was found (%2); the interface stays in Chinese.</translation>
            </message>
            <message>
                <source>%1 的语言包是空的或解析失败，界面仍然是中文。</source>
                <translation>The %1 language pack is empty or failed to parse; the interface stays in Chinese.</translation>
            </message>
        <message>
            <source>没有指定语言。</source>
            <translation>No language was specified.</translation>
        </message>
        <message>
            <source>读不了 %1：%2</source>
            <translation>Cannot read %1: %2</translation>
        </message>
        <message>
            <source>%1 不是有效的语言包（解析失败或没有词条）。</source>
            <translation>%1 is not a valid language pack (it failed to parse or has no entries).</translation>
        </message>
        <message>
            <source>%1 不是本应用的语言包（里面没有 aipdf::app:: 的词条）。</source>
            <translation>%1 is not a language pack for this application (it has no aipdf::app:: entries).</translation>
        </message>
        <message>
            <source>这份语言包是 %1 的，不是 %2 的：%3</source>
            <translation>This language pack is for %1, not %2: %3</translation>
        </message>
        <message>
            <source>中文是源语言，不需要语言包。</source>
            <translation>Chinese is the source language; it does not need a language pack.</translation>
        </message>
        <message>
            <source>%1 不像语言代码（应该像 ja、zh_CN 这样）。</source>
            <translation>%1 does not look like a language code (it should look like ja or zh_CN).</translation>
        </message>
        <message>
            <source>建不了语言包目录 %1。</source>
            <translation>Cannot create the language pack directory %1.</translation>
        </message>
        <message>
            <source>换不掉旧的语言包 %1。</source>
            <translation>Cannot replace the old language pack %1.</translation>
        </message>
        <message>
            <source>把语言包复制到 %1 失败。</source>
            <translation>Failed to copy the language pack to %1.</translation>
        </message>
        <message>
            <source>选择语言包文件</source>
            <translation>Select Language Pack File</translation>
        </message>
        <message>
            <source>语言包 (*.ts)</source>
            <translation>Language pack (*.ts)</translation>
        </message>
        <message>
            <source>安装语言包</source>
            <translation>Install Language Pack</translation>
        </message>
        <message>
            <source>%1 读不出语言代码：它可能不是标准的 .ts 翻译目录。</source>
            <translation>Cannot read the language code from %1: it may not be a standard .ts translation catalog.</translation>
        </message>
        <message>
            <source>%1 的语言包安装失败：%2</source>
            <translation>Failed to install the language pack for %1: %2</translation>
        </message>
        <message>
            <source>%1 的语言包装好了，但切换失败：%2</source>
            <translation>The language pack for %1 is installed, but switching failed: %2</translation>
        </message>
        <message>
            <source>已安装 %1 的语言包，可以在「设置 → 界面语言」里切换。</source>
            <translation>The language pack for %1 is installed. You can switch to it in Settings → Interface Language.</translation>
        </message>
        <message>
            <source>没有找到 %1 的语言包，界面仍然是中文。把语言包文件（%2）放到这个目录：%3；或者在「语言 → 安装语言包…」/「设置 → 界面语言」里点「安装语言包…」直接选文件安装。</source>
            <translation>The language pack for %1 was not found; the interface is still in Chinese. Put the language pack file (%2) in this directory: %3; or click “Install Language Pack…” under Language → Install Language Pack / Settings → Interface Language to pick the file directly.</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::ConversionInputs</name>
            <message>
                <source>从剪贴板新建 PDF</source>
                <translation>New PDF from clipboard</translation>
            </message>
            <message>
                <source>从网页新建 PDF</source>
                <translation>New PDF from web page</translation>
            </message>
            <message>
                <source>从表单文件新建 PDF</source>
                <translation>New PDF from form data file</translation>
            </message>
            <message>
                <source>剪贴板内容（只读预览）：</source>
                <translation>Clipboard content (read-only preview):</translation>
            </message>
            <message>
                <source>剪贴板内容</source>
                <translation>Clipboard content</translation>
            </message>
            <message>
                <source>剪贴板里没有文本，没法生成 PDF。</source>
                <translation>There is no text in the clipboard, so no PDF can be generated.</translation>
            </message>
            <message>
                <source>剪贴板.pdf</source>
                <translation>Clipboard.pdf</translation>
            </message>
                    <message>
            <source>浏览器引擎没跑起来（%1），这次按网页正文里的文字排版：JavaScript 没有执行，CSS 没有渲染。</source>
            <translation>The browser engine did not start (%1), so this time the text of the page body was laid out instead: JavaScript was not executed and CSS was not rendered.</translation>
        </message>
        <message>
            <source>本机没有可用的浏览器引擎（Edge / Chrome），这次按网页正文里的文字排版：JavaScript 没有执行，CSS 没有渲染。</source>
            <translation>There is no usable browser engine on this machine (Edge / Chrome), so this time the text of the page body was laid out instead: JavaScript was not executed and CSS was not rendered.</translation>
        </message>
<message>
                <source>网页地址</source>
                <translation>Web address</translation>
            </message>
            <message>
                <source>还没有网址。</source>
                <translation>No web address yet.</translation>
            </message>
            <message>
                <source>网址无效。</source>
                <translation>Invalid web address.</translation>
            </message>
            <message>
                <source>只支持 http / https 网址。</source>
                <translation>Only http / https addresses are supported.</translation>
            </message>
            <message>
                <source>等了 %1 秒还没拿到网页，已经放弃。</source>
                <translation>Waited %1 seconds for the page and gave up.</translation>
            </message>
                    <message>
            <source>这次用浏览器引擎 %1 渲染：JavaScript 已执行，CSS 已渲染，图片按渲染结果落位。</source>
            <translation>This time it was rendered with browser engine %1: JavaScript was executed, CSS was rendered, and images were placed according to the rendered result.</translation>
        </message>
<message>
                <source>连不上这个网址：%1</source>
                <translation>Could not reach this address: %1</translation>
            </message>
            <message>
                <source>服务器返回 %1（不是 200），没拿到正文。</source>
                <translation>The server returned %1 (not 200), so no content was fetched.</translation>
            </message>
            <message>
                <source>这个地址返回的不是网页（%1），不能转成 PDF。</source>
                <translation>This address did not return a web page (%1), so it cannot be converted to PDF.</translation>
            </message>
            <message>
                <source>这个地址返回的是空的，没有正文。</source>
                <translation>This address returned nothing, so there is no content.</translation>
            </message>
            <message>
                <source>这个网页不是 UTF-8 编码，正文读出来会是乱码；本程序只支持 UTF-8 / UTF-16。</source>
                <translation>This page is not UTF-8 encoded, so its text would be garbled; only UTF-8 / UTF-16 are supported.</translation>
            </message>
            <message>
                <source>这个网页的编码是 %1，本程序只支持 UTF-8 / UTF-16。</source>
                <translation>This page uses the %1 encoding; only UTF-8 / UTF-16 are supported.</translation>
            </message>
            <message>
                <source>模板 PDF</source>
                <translation>Template PDF</translation>
            </message>
            <message>
                <source>表单数据文件</source>
                <translation>Form data file</translation>
            </message>
            <message>
                <source>没有选模板 PDF。</source>
                <translation>No template PDF was selected.</translation>
            </message>
            <message>
                <source>没有选表单数据文件。</source>
                <translation>No form data file was selected.</translation>
            </message>
            <message>
                <source>读不了这个文件：%1</source>
                <translation>Cannot read this file: %1</translation>
            </message>
            <message>
                <source>填上了 %1 个字段：%2</source>
                <translation>Filled %1 field(s): %2</translation>
            </message>
            <message>
                <source>填不上的字段 %1：%2</source>
                <translation>Could not fill field %1: %2</translation>
            </message>
            <message>
                <source>模板里 %1 个字段没有数据，保持原样。</source>
                <translation>%1 field(s) in the template had no data and were left unchanged.</translation>
            </message>
            <message>
                <source>选择文件…</source>
                <translation>Browse…</translation>
            </message>
            <message>
                <source>选择模板 PDF</source>
                <translation>Select template PDF</translation>
            </message>
            <message>
                <source>选择表单数据文件</source>
                <translation>Select form data file</translation>
            </message>
            <message>
                <source>表单数据文件 (*.fdf *.xfdf *.xml)</source>
                <translation>Form data files (*.fdf *.xfdf *.xml)</translation>
            </message>
            <message>
                <source>PDF 文件 (*.pdf)</source>
                <translation>PDF files (*.pdf)</translation>
            </message>
            <message>
                <source>字体文件</source>
                <translation>Font file</translation>
            </message>
            <message>
                <source>字体文件（可选，TTF）</source>
                <translation>Font file (optional, TTF)</translation>
            </message>
            <message>
                <source>选择字体文件</source>
                <translation>Select font file</translation>
            </message>
            <message>
                <source>字体文件 (*.ttf)</source>
                <translation>Font files (*.ttf)</translation>
            </message>
            <message>
                <source>生成 PDF</source>
                <translation>Generate PDF</translation>
            </message>
            <message>
                <source>转换结果说明</source>
                <translation>Conversion result</translation>
            </message>
            <message>
                <source>还没有生成 PDF。</source>
                <translation>No PDF has been generated yet.</translation>
            </message>
            <message>
                <source>已生成 %1 页。</source>
                <translation>%1 page(s) generated.</translation>
            </message>
            <message>
                <source>已嵌入系统字体 %1 来显示中文。</source>
                <translation>Embedded the system font %1 so that Chinese can be displayed.</translation>
            </message>
            <message>
                <source>没有图形界面环境，读不了剪贴板。</source>
                <translation>There is no GUI environment, so the clipboard cannot be read.</translation>
            </message>
            <message>
                <source>用户取消了转换。</source>
                <translation>The conversion was cancelled.</translation>
            </message>
            <message>
                <source>文档</source>
                <translation>Document</translation>
            </message>
                <message>
                <source>剪贴板里的富文本没有正文（去掉标签和样式之后就没东西了），没法生成 PDF。</source>
                <translation>The rich text in the clipboard has no body text (nothing is left after tags and styles are removed), so no PDF can be created.</translation>
            </message>
            <message>
                <source>剪贴板里只有图片，本程序还不支持把图片转成 PDF（只支持文字和富文本）。</source>
                <translation>The clipboard contains only an image; this program cannot turn images into PDF yet (only text and rich text are supported).</translation>
            </message>
            <message>
                <source>这次按富文本排版（保留了标题、段落和列表结构）。</source>
                <translation>Laid out as rich text this time (headings, paragraphs and list structure are preserved).</translation>
            </message>
            <message>
                <source>这次按纯文本排版。</source>
                <translation>Laid out as plain text this time.</translation>
            </message>
            <message>
                <source>这个网页里没有正文（脚本、样式和导航都去掉之后就没东西了）。</source>
                <translation>This web page has no body text (nothing is left after scripts, styles and navigation are removed).</translation>
            </message>
            <message>
                <source>这次带了 %1 张图片。</source>
                <translation>%1 image(s) were included this time.</translation>
            </message>
            <message>
                <source>这次没有带图片。</source>
                <translation>No images were included this time.</translation>
            </message>
            <message>
                <source>这次没有带图片（网页里没有图片）。</source>
                <translation>No images were included this time (the web page has no images).</translation>
            </message>
            <message>
                <source>图片没带进来：%1（%2）</source>
                <translation>Image not included: %1 (%2)</translation>
            </message>
            <message>
                <source>下载失败</source>
                <translation>the download failed</translation>
            </message>
            <message>
                <source>格式不支持</source>
                <translation>the format is not supported</translation>
            </message>
            <message>
                <source>数据损坏，解不开</source>
                <translation>the data is corrupted and cannot be decoded</translation>
            </message>
            <message>
                <source>图片太大，一页放不下</source>
                <translation>the image is too large to fit on a page</translation>
            </message>
            <message>
                <source>排不进这份 PDF</source>
                <translation>it cannot be placed into this PDF</translation>
            </message>
            <message>
                <source>页面里的图太多，超出了单次上限</source>
                <translation>the page has too many images (over the limit for one run)</translation>
            </message>
            <message>
                <source>剪贴板里没有图片，没法生成 PDF。</source>
                <translation>There are no images in the clipboard, so no PDF can be created.</translation>
            </message>
            <message>
                <source>这次按图片排版（每张图一页）。</source>
                <translation>Laid out as images (one image per page).</translation>
            </message>
            <message>
                <source>有 %1 张图片没能带进来：</source>
                <translation>%1 image(s) could not be included:</translation>
            </message>
            <message>
                <source>第 %1 张图片：%2</source>
                <translation>Image %1: %2</translation>
            </message>
            <message>
                <source>没有内容</source>
                <translation>No content</translation>
            </message>
            <message>
                <source>格式不支持（%1）</source>
                <translation>Unsupported format (%1)</translation>
            </message>
            <message>
                <source>这次带的图片太多，超出了单次上限</source>
                <translation>Too many images for a single conversion</translation>
            </message>
            <message>
                <source>剪贴板里还有 %1 张图片，这次按文字排版，图片没有进 PDF。</source>
                <translation>The clipboard also has %1 image(s); this time it was laid out as text, so the images are not in the PDF.</translation>
            </message>
            <message>
                <source>剪贴板内容类型</source>
                <translation>Clipboard content type</translation>
            </message>
            <message>
                <source>自动（有文字用文字，没文字用图片）</source>
                <translation>Automatic (text if present, otherwise images)</translation>
            </message>
            <message>
                <source>文字（富文本优先）</source>
                <translation>Text (rich text preferred)</translation>
            </message>
            <message>
                <source>图片（每张一页）</source>
                <translation>Images (one per page)</translation>
            </message>
        <message>
            <source>首选的浏览器引擎没跑起来，换了另一个引擎渲染。</source>
            <translation>The preferred browser engine did not start; another engine was used for rendering.</translation>
        </message>
</context>
    <context>
        <name>aipdf::app::DocumentCompareDialog</name>
                    <message>
            <source>块 %1 为新增块</source>
            <translation>Block %1 is a new block</translation>
        </message>
        <message>
            <source>块 %1 已删除</source>
            <translation>Block %1 was deleted</translation>
        </message>
        <message>
            <source>块 %1 从页 %2 %3 移到页 %4 %5</source>
            <translation>Block %1 moved from page %2 %3 to page %4 %5</translation>
        </message>
        <message>
            <source>换图</source>
            <translation>Image replaced</translation>
        </message>
        <message>
            <source>删除块</source>
            <translation>Deleted block</translation>
        </message>
<message>
                <source>插入</source>
                <translation>Inserted</translation>
            </message>
            <message>
                <source>删除</source>
                <translation>Deleted</translation>
            </message>
            <message>
                <source>修改</source>
                <translation>Modified</translation>
            </message>
                    <message>
            <source>文字改写</source>
            <translation>Text rewritten</translation>
        </message>
        <message>
            <source>新增块</source>
            <translation>New block</translation>
        </message>
<message>
                <source>新增页</source>
                <translation>Page added</translation>
            </message>
            <message>
                <source>删除页</source>
                <translation>Page removed</translation>
            </message>
            <message>
                <source>差异</source>
                <translation>Difference</translation>
            </message>
            <message>
                <source>原文：</source>
                <translation>Original:</translation>
            </message>
            <message>
                <source>新文：</source>
                <translation>Revised:</translation>
            </message>
                    <message>
            <source>版面差异：共 %1 处（移动 %2 处、换图 %3 处、表格结构变化 %4 处、块新增 %5 处、块删除 %6 处）。</source>
            <translation>Layout differences: %1 in total (%2 moved, %3 images replaced, %4 table structure changes, %5 new blocks, %6 deleted blocks).</translation>
        </message>
        <message>
            <source>版面差异：%1</source>
            <translation>Layout differences: %1</translation>
        </message>
        <message>
            <source>表格第 %1 行第 %2 列已改写</source>
            <translation>Row %1, column %2 of the table was rewritten</translation>
        </message>
        <message>
            <source>表格列数由 %1 变为 %2</source>
            <translation>Table columns changed from %1 to %2</translation>
        </message>
        <message>
            <source>表格行数由 %1 变为 %2</source>
            <translation>Table rows changed from %1 to %2</translation>
        </message>
        <message>
            <source>此块文字已改写</source>
            <translation>The text of this block was rewritten</translation>
        </message>
        <message>
            <source>此位置的图片已被替换</source>
            <translation>The image at this position was replaced</translation>
        </message>
        <message>
            <source>表格单元格改写</source>
            <translation>Table cell rewritten</translation>
        </message>
        <message>
            <source>表格列变化</source>
            <translation>Table columns changed</translation>
        </message>
        <message>
            <source>表格行变化</source>
            <translation>Table rows changed</translation>
        </message>
        <message>
            <source>移动</source>
            <translation>Moved</translation>
        </message>
        <message>
            <source>版面差异</source>
            <translation>Layout differences</translation>
        </message>
<message>
                <source>选一份旧版本的 PDF</source>
                <translation>Choose an older PDF version</translation>
            </message>
            <message>
                <source>选一份新版本的 PDF</source>
                <translation>Choose a newer PDF version</translation>
            </message>
            <message>
                <source>浏览…</source>
                <translation>Browse…</translation>
            </message>
            <message>
                <source>开始比较</source>
                <translation>Start comparing</translation>
            </message>
            <message>
                <source>关闭</source>
                <translation>Close</translation>
            </message>
            <message>
                <source>类型</source>
                <translation>Type</translation>
            </message>
            <message>
                <source>页码</source>
                <translation>Page</translation>
            </message>
            <message>
                <source>原文</source>
                <translation>Original</translation>
            </message>
            <message>
                <source>新文</source>
                <translation>Revised</translation>
            </message>
            <message>
                <source>相似度</source>
                <translation>Similarity</translation>
            </message>
            <message>
                <source>第 %1 页</source>
                <translation>Page %1</translation>
            </message>
            <message>
                <source>比较文档</source>
                <translation>Compare documents</translation>
            </message>
            <message>
                <source>请先选两份 PDF。</source>
                <translation>Please choose two PDF files first.</translation>
            </message>
            <message>
                <source>选择原文 PDF</source>
                <translation>Choose the original PDF</translation>
            </message>
            <message>
                <source>选择新文 PDF</source>
                <translation>Choose the revised PDF</translation>
            </message>
            <message>
                <source>PDF 文件 (*.pdf)</source>
                <translation>PDF files (*.pdf)</translation>
            </message>
            <message>
                <source>比较失败：%1</source>
                <translation>Comparison failed: %1</translation>
            </message>
            <message>
                <source>选两份 PDF，然后点「开始比较」。</source>
                <translation>Choose two PDF files, then click "Start comparing".</translation>
            </message>
            <message>
                <source>比较完成：两份文件的文字没有差异。</source>
                <translation>Comparison complete: the two documents have no text differences.</translation>
            </message>
            <message>
                <source>比较完成：共 %1 处差异（新增 %2 处、删除 %3 处、修改 %4 处、新增页 %5 页、删除页 %6 页），整体相似度 %7%。</source>
                <translation>Comparison complete: %1 differences in total (%2 inserted, %3 deleted, %4 modified, %5 pages added, %6 pages removed); overall similarity %7.</translation>
            </message>
                <message>
                <source>同时比较图形（渲染成位图逐像素比，较慢）</source>
                <translation>Also compare graphics (render to bitmaps and compare pixel by pixel; slower)</translation>
            </message>
            <message>
                <source>文字差异</source>
                <translation>Text differences</translation>
            </message>
            <message>
                <source>视觉差异（图形）</source>
                <translation>Visual differences (graphics)</translation>
            </message>
            <message>
                <source>差异比例</source>
                <translation>Difference ratio</translation>
            </message>
            <message>
                <source>差异区域</source>
                <translation>Difference area</translation>
            </message>
            <message>
                <source>说明</source>
                <translation>Details</translation>
            </message>
            <message>
                <source>x %1..%2, y %3..%4</source>
                <translation>x %1..%2, y %3..%4</translation>
            </message>
            <message>
                <source>图形有变化</source>
                <translation>Graphics changed</translation>
            </message>
            <message>
                <source>没有图形差异</source>
                <translation>No graphics differences</translation>
            </message>
            <message>
                <source>无法比对：%1</source>
                <translation>Cannot compare: %1</translation>
            </message>
            <message>
                <source>（两页尺寸不同，只比了共同区域）</source>
                <translation>(the two pages differ in size; only the common area was compared)</translation>
            </message>
            <message>
                <source>视觉差异：%1 页（平均差异比例 %2%，最大 %3%）。</source>
                <translation>Visual differences: %1 page(s) (average difference ratio %2%, maximum %3%).</translation>
            </message>
            <message>
                <source>视觉差异：没有发现图形改动。</source>
                <translation>Visual differences: no graphics changes found.</translation>
            </message>
            <message>
                <source>有 %1 页无法比对（渲染失败）。</source>
                <translation>%1 page(s) could not be compared (rendering failed).</translation>
            </message>
            <message>
                <source>像素比对失败：%1</source>
                <translation>Pixel comparison failed: %1</translation>
            </message>
            <message>
                <source>像素比对说明：%1</source>
                <translation>Pixel comparison note: %1</translation>
            </message>
            <message>
                <source>自动对齐</source>
                <translation>Auto-align pages</translation>
            </message>
            <message>
                <source>已按内容自动对齐：原文独有 %1 页、新文独有 %2 页。</source>
                <translation>Aligned by content: %1 page(s) only in the original, %2 page(s) only in the revised.</translation>
            </message>
            <message>
                <source>自动对齐未生效，已退回按页码位置配对。</source>
                <translation>Auto-alignment was not applied; fell back to page position pairing.</translation>
            </message>
            <message>
                <source>自动对齐不比按位置更准。</source>
                <translation>Auto-alignment was no more accurate than pairing by page position.</translation>
            </message>
            <message>
                <source>已关闭自动对齐。</source>
                <translation>Auto-alignment was turned off.</translation>
            </message>
            <message>
                <source>页数超过对齐上限。</source>
                <translation>The page count exceeds the alignment limit.</translation>
            </message>
</context>
    <context>
        <name>aipdf::app::ScannerCapture</name>
            <message>
                <source>扫描完成</source>
                <translation>Scan complete</translation>
            </message>
            <message>
                <source>无法启动 Windows 图像采集（WIA），本机可能没有安装扫描仪支持</source>
                <translation>Windows Image Acquisition (WIA) could not be started; this computer may not have scanner support installed</translation>
            </message>
            <message>
                <source>没有找到扫描仪</source>
                <translation>No scanner was found</translation>
            </message>
            <message>
                <source>已取消扫描，没有生成 PDF</source>
                <translation>Scan cancelled; no PDF was created</translation>
            </message>
            <message>
                <source>从扫描仪取图失败，请检查设备是否被占用或卡纸</source>
                <translation>Failed to get an image from the scanner; check that the device is not in use and has no paper jam</translation>
            </message>
            <message>
                <source>扫描仪返回的图像格式不受支持（本程序只支持 BMP 与 JPEG）</source>
                <translation>The scanner returned an unsupported image format (only BMP and JPEG are supported)</translation>
            </message>
            <message>
                <source>扫描结果转成 PDF 失败</source>
                <translation>Failed to convert the scan result to PDF</translation>
            </message>
            <message>
                <source>（技术细节：%1）</source>
                <translation>(technical details: %1)</translation>
            </message>
            <message>
                <source>未命名设备</source>
                <translation>Unnamed device</translation>
            </message>
            <message>
                <source>%1（非扫描仪）</source>
                <translation>%1 (not a scanner)</translation>
            </message>
            <message>
                <source>从扫描仪新建 PDF</source>
                <translation>New PDF from Scanner</translation>
            </message>
            <message>
                <source>扫描仪</source>
                <translation>Scanner</translation>
            </message>
            <message>
                <source>扫描分辨率</source>
                <translation>Scan resolution</translation>
            </message>
            <message>
                <source> DPI</source>
                <translation>DPI</translation>
            </message>
            <message>
                <source>刷新设备</source>
                <translation>Refresh devices</translation>
            </message>
            <message>
                <source>扫描</source>
                <translation>Scan</translation>
            </message>
            <message>
                <source>生成 PDF</source>
                <translation>Create PDF</translation>
            </message>
            <message>
                <source>取消</source>
                <translation>Cancel</translation>
            </message>
            <message>
                <source>选择设备后点「扫描」。</source>
                <translation>Choose a device, then click Scan.</translation>
            </message>
            <message>
                <source>找到 %1 台图像设备。</source>
                <translation>Found %1 imaging device(s).</translation>
            </message>
            <message>
                <source>请先选择一台设备</source>
                <translation>Please choose a device first</translation>
            </message>
            <message>
                <source>已扫描 %1 页，可以点「生成 PDF」。</source>
                <translation>Scanned %1 page(s). Click Create PDF.</translation>
            </message>
                <message>
                <source> DPI</source>
                <translation>DPI</translation>
            </message>
            <message>
                <source> DPI</source>
                <translation>DPI</translation>
            </message>
            <message>
                <source> DPI</source>
                <translation>DPI</translation>
            </message>
            <message>
                <source> DPI</source>
                <translation>DPI</translation>
            </message>
            <message>
                <source>扫描仪返回的图像格式不受支持（本程序只支持 BMP、JPEG 与 PNG）</source>
                <translation>The scanner returned an unsupported image format (only BMP, JPEG and PNG are supported)</translation>
            </message>
            <message>
                <source> DPI</source>
                <translation>DPI</translation>
            </message>
            <message>
                <source> DPI</source>
                <translation>DPI</translation>
            </message>
            <message>
                <source> DPI</source>
                <translation>DPI</translation>
            </message>
            <message>
                <source> DPI</source>
                <translation>DPI</translation>
            </message>
            <message>
                <source> DPI</source>
                <translation>DPI</translation>
            </message>
            <message>
                <source> DPI</source>
                <translation>DPI</translation>
            </message>
            <message>
                <source> DPI</source>
                <translation>DPI</translation>
            </message>
</context>
    <context>
        <name>aipdf::app::PresentationLoop</name>
            <message>
                <source>放映（循环）</source>
                <translation>Presentation (Loop)</translation>
            </message>
            <message>
                <source>放映到最后一页后自动回到第一页</source>
                <translation>Loop back to the first page after the last slide</translation>
            </message>
            <message>
                <source>放映到最后一页后停住</source>
                <translation>Stop at the last slide</translation>
            </message>
    </context>
    <context>
        <name>aipdf::app::OneNoteExporter</name>
            <message>
                <source>未检测到本机安装 OneNote，无法把页面发送到 OneNote。请先安装 Microsoft OneNote 桌面版后重试。</source>
                <translation>OneNote is not installed on this computer, so the pages cannot be sent to it. Install the Microsoft OneNote desktop app and try again.</translation>
            </message>
            <message>
                <source>OneNote 已安装，但无法连接 OneNote 的后台接口。请先手动打开一次 OneNote 完成首次设置后重试。</source>
                <translation>OneNote is installed, but its background interface could not be reached. Open OneNote once manually to finish the first-run setup, then try again.</translation>
            </message>
            <message>
                <source>（技术细节：%1）</source>
                <translation>(technical details: %1)</translation>
            </message>
            <message>
                <source>发送到 OneNote 失败。</source>
                <translation>Failed to send to OneNote.</translation>
            </message>
            <message>
                <source>发送到 OneNote 失败：%1</source>
                <translation>Failed to send to OneNote: %1</translation>
            </message>
            <message>
                <source>（没有可导出的页面）</source>
                <translation>(no pages to export)</translation>
            </message>
            <message>
                <source>第 %1 页</source>
                <translation>Page %1</translation>
            </message>
            <message>
                <source>PNG 图片</source>
                <translation>PNG image</translation>
            </message>
            <message>
                <source>JPEG 图片</source>
                <translation>JPEG image</translation>
            </message>
            <message>
                <source>%1 - 第 %2 页</source>
                <translation>%1 - Page %2</translation>
            </message>
            <message>
                <source>已把 %1 导出为 %2（%3 DPI），并发送到 OneNote 的「%4」，共新建 %5 页。</source>
                <translation>Exported %1 as %2 (%3 DPI) and sent them to "%4" in OneNote; %5 new page(s) created.</translation>
            </message>
            <message>
                <source>读取 OneNote 的笔记本结构失败（%1）</source>
                <translation>Failed to read the notebook structure from OneNote (%1)</translation>
            </message>
            <message>
                <source>OneNote 里没有找到可用的分区</source>
                <translation>No usable section was found in OneNote</translation>
            </message>
            <message>
                <source>在 OneNote 里新建页面失败（%1）</source>
                <translation>Failed to create a new page in OneNote (%1)</translation>
            </message>
            <message>
                <source>把页面写入 OneNote 失败（%1）</source>
                <translation>Failed to write the page into OneNote (%1)</translation>
            </message>
            <message>
                <source>当前线程的 COM 模式与 OneNote 需要的单线程单元冲突</source>
                <translation>The COM apartment mode of the current thread conflicts with the single-threaded apartment OneNote requires</translation>
            </message>
            <message>
                <source>初始化 COM 失败（%1）</source>
                <translation>Failed to initialize COM (%1)</translation>
            </message>
            <message>
                <source>找不到 OneNote 的 COM 接口（%1）</source>
                <translation>The OneNote COM interface could not be found (%1)</translation>
            </message>
            <message>
                <source>无法启动 OneNote 进程（%1）</source>
                <translation>Failed to start the OneNote process (%1)</translation>
            </message>
            <message>
                <source>OneNote 桌面版只支持 Windows</source>
                <translation>The OneNote desktop app is only available on Windows</translation>
            </message>
            <message>
                <source>没有可导出的页面</source>
                <translation>There are no pages to export</translation>
            </message>
            <message>
                <source>缺少页面渲染回调</source>
                <translation>The page renderer callback is missing</translation>
            </message>
            <message>
                <source>第 %1 页渲染成图片失败</source>
                <translation>Rendering page %1 into an image failed</translation>
            </message>
            <message>
                <source>第 %1 页编码成 %2 失败</source>
                <translation>Encoding page %1 as %2 failed</translation>
            </message>
                <message>
                <source>（已送出 %1 页）</source>
                <translation>(%1 page(s) were already sent)</translation>
            </message>
            <message>
                <source>（已经写进 OneNote %1 页，剩下的没有写进去）</source>
                <translation>(%1 page(s) were written into OneNote; the remaining pages were not written)</translation>
            </message>
            <message>
                <source>第 %1 页的文字层写坏了（文字里有非法控制字符），这一页只送图片</source>
                <translation>The text layer of page %1 is broken (the text contains illegal control characters); only the image was sent for this page</translation>
            </message>
            <message>
                <source>取第 %1 页的文字时出错</source>
                <translation>The text of page %1 could not be read</translation>
            </message>
            <message>
                <source>（这次没有文字层，在 OneNote 里搜不到文字）</source>
                <translation>(There is no text layer this time, so the text cannot be searched for in OneNote)</translation>
            </message>
            <message>
                <source>（%1 页的文字层写不进去，已经只送图片）</source>
                <translation>(The text layer could not be written for %1 page(s); only the images were sent)</translation>
            </message>
            <message>
                <source>这一页的 XML 写坏了，已经只送图片。</source>
                <translation>The XML of this page is broken; only the image was sent.</translation>
            </message>
</context>
    <context>
        <name>aipdf::app::PdfAPreflightPanel</name>
            <message>
                <source>文档是空的</source>
                <translation>The document is empty.</translation>
            </message>
            <message>
                <source>不是 PDF 文件</source>
                <translation>This file is not a PDF.</translation>
            </message>
            <message>
                <source>本次构建没有启用 qpdf</source>
                <translation>This build was made without qpdf.</translation>
            </message>
            <message>
                <source>文档读不出来</source>
                <translation>The document could not be read.</translation>
            </message>
            <message>
                <source>文件头版本符合要求</source>
                <translation>The file header version is acceptable.</translation>
            </message>
            <message>
                <source>文件头版本不是 %PDF-1.7</source>
                <translation>The file header is not %PDF-1.7.</translation>
            </message>
            <message>
                <source>trailer 里有 /ID</source>
                <translation>The trailer has an /ID.</translation>
            </message>
            <message>
                <source>trailer 里缺 /ID</source>
                <translation>The trailer is missing /ID.</translation>
            </message>
            <message>
                <source>文档没有加密</source>
                <translation>The document is not encrypted.</translation>
            </message>
            <message>
                <source>文档被加密了</source>
                <translation>The document is encrypted.</translation>
            </message>
            <message>
                <source>缺少 XMP 元数据</source>
                <translation>XMP metadata is missing.</translation>
            </message>
            <message>
                <source>XMP 元数据的类型不对</source>
                <translation>The XMP metadata stream has the wrong subtype.</translation>
            </message>
            <message>
                <source>XMP 里没有声明 PDF/A</source>
                <translation>The XMP metadata does not declare PDF/A.</translation>
            </message>
            <message>
                <source>XMP 声明的 PDF/A 部分不对</source>
                <translation>The XMP metadata declares the wrong PDF/A part.</translation>
            </message>
            <message>
                <source>XMP 声明的一致级别不对</source>
                <translation>The XMP metadata declares the wrong conformance level.</translation>
            </message>
            <message>
                <source>XMP 声明正确</source>
                <translation>The XMP metadata declaration is correct.</translation>
            </message>
            <message>
                <source>缺少输出意图（OutputIntent）</source>
                <translation>The output intent (OutputIntent) is missing.</translation>
            </message>
            <message>
                <source>输出意图的类型不对</source>
                <translation>The output intent has the wrong subtype.</translation>
            </message>
            <message>
                <source>输出意图里没有嵌入 ICC 配置文件</source>
                <translation>The output intent has no embedded ICC profile.</translation>
            </message>
            <message>
                <source>嵌入的 ICC 配置文件不合要求</source>
                <translation>The embedded ICC profile does not meet the requirements.</translation>
            </message>
            <message>
                <source>输出意图符合要求</source>
                <translation>The output intent is acceptable.</translation>
            </message>
            <message>
                <source>字体全部内嵌</source>
                <translation>All fonts are embedded.</translation>
            </message>
            <message>
                <source>有字体没有内嵌</source>
                <translation>Some fonts are not embedded.</translation>
            </message>
            <message>
                <source>没有用 LZW 压缩</source>
                <translation>No LZW compression is used.</translation>
            </message>
            <message>
                <source>用了 LZW 压缩</source>
                <translation>LZW compression is used.</translation>
            </message>
            <message>
                <source>没有外部文件引用</source>
                <translation>There are no external file references.</translation>
            </message>
            <message>
                <source>有外部文件引用</source>
                <translation>There are external file references.</translation>
            </message>
            <message>
                <source>没有 JavaScript 动作</source>
                <translation>There are no JavaScript actions.</translation>
            </message>
            <message>
                <source>有 JavaScript 动作</source>
                <translation>There are JavaScript actions.</translation>
            </message>
            <message>
                <source>色彩空间符合要求</source>
                <translation>The colour spaces are acceptable.</translation>
            </message>
            <message>
                <source>用了不能用的色彩空间</source>
                <translation>A colour space that PDF/A does not allow is in use.</translation>
            </message>
            <message>
                <source>没有透明对象</source>
                <translation>There is no transparency.</translation>
            </message>
            <message>
                <source>透明对象符合要求</source>
                <translation>The transparency is acceptable.</translation>
            </message>
            <message>
                <source>有透明对象但没有输出意图</source>
                <translation>Transparency is used without an output intent.</translation>
            </message>
            <message>
                <source>有透明对象但页面缺透明组</source>
                <translation>Transparency is used without a page transparency group.</translation>
            </message>
            <message>
                <source>软掩码的 /TR 不是 /Identity</source>
                <translation>A soft mask /TR is not /Identity.</translation>
            </message>
            <message>
                <source>注解符合要求</source>
                <translation>The annotations are acceptable.</translation>
            </message>
            <message>
                <source>用了 PDF/A 禁止的注解</source>
                <translation>An annotation type that PDF/A forbids is in use.</translation>
            </message>
            <message>
                <source>注解缺外观流</source>
                <translation>An annotation is missing its appearance stream.</translation>
            </message>
            <message>
                <source>注解的标志位不对</source>
                <translation>An annotation has unsuitable flags.</translation>
            </message>
            <message>
                <source>不通过</source>
                <translation>Failed</translation>
            </message>
            <message>
                <source>警告</source>
                <translation>Warnings</translation>
            </message>
            <message>
                <source>通过</source>
                <translation>Passed</translation>
            </message>
            <message>
                <source>码</source>
                <translation>Code</translation>
            </message>
            <message>
                <source>条款</source>
                <translation>Clause</translation>
            </message>
            <message>
                <source>标题</source>
                <translation>Title</translation>
            </message>
            <message>
                <source>说明</source>
                <translation>Details</translation>
            </message>
            <message>
                <source>建议</source>
                <translation>Suggestion</translation>
            </message>
            <message>
                <source>PDF/A-2b 预检</source>
                <translation>PDF/A-2b Preflight</translation>
            </message>
            <message>
                <source>预检结论</source>
                <translation>Preflight conclusion</translation>
            </message>
            <message>
                <source>预检能力说明</source>
                <translation>What this preflight covers</translation>
            </message>
            <message>
                <source>预检结论明细</source>
                <translation>Preflight findings</translation>
            </message>
            <message>
                <source>PDF/A-2b 预检发现 %1 个问题（通过 %2 项、警告 %3 项、不通过 %4 项）。</source>
                <translation>The PDF/A-2b preflight found %1 issue(s) (%2 passed, %3 warnings, %4 failed).</translation>
            </message>
            <message>
                <source>文件自己声明的是 PDF/A-%1%2。</source>
                <translation>The file declares itself as PDF/A-%1%2.</translation>
            </message>
            <message>
                <source>这是预检，不是合规认证：它只检查文件结构里能读出来的那些要求。</source>
                <translation>This is a preflight, not a conformance certification: it only checks the requirements that can be read from the file structure.</translation>
            </message>
            <message>
                <source> 建议：%1</source>
                <translation>Suggestion: %1</translation>
            </message>
                <message>
                <source> 建议：%1</source>
                <translation>Suggestion: %1</translation>
            </message>
            <message>
                <source> 建议：%1</source>
                <translation>Suggestion: %1</translation>
            </message>
            <message>
                <source> 建议：%1</source>
                <translation>Suggestion: %1</translation>
            </message>
            <message>
                <source> 建议：%1</source>
                <translation>Suggestion: %1</translation>
            </message>
</context>
    <context>
        <name>aipdf::app::HiddenDataDialog</name>
            <message>
                <source>清理隐藏信息</source>
                <translation>Clean hidden data</translation>
            </message>
            <message>
                <source>类别</source>
                <translation>Category</translation>
            </message>
            <message>
                <source>这份文档里有</source>
                <translation>In this document</translation>
            </message>
            <message>
                <source>清理</source>
                <translation>Clean</translation>
            </message>
            <message>
                <source>取消</source>
                <translation>Cancel</translation>
            </message>
            <message>
                <source>勾掉要清理的类别。页面正文、可见批注和表单域结构都会保留；嵌入文件与附件默认不勾，勾了才会删。</source>
                <translation>Uncheck the categories to clean. Page content, visible annotations and form structure are kept; embedded files and attachments are unchecked by default and are only removed if you check them.</translation>
            </message>
            <message>
                <source>这份文档里没有</source>
                <translation>None in this document</translation>
            </message>
            <message>
                <source>%1 项</source>
                <translation>%1 item(s)</translation>
            </message>
            <message>
                <source>文档属性（标题、作者、主题等）</source>
                <translation>Document properties (title, author, subject, ...)</translation>
            </message>
            <message>
                <source>元数据流（XMP 等）</source>
                <translation>Metadata streams (XMP, ...)</translation>
            </message>
            <message>
                <source>增量历史里的旧版本</source>
                <translation>Old revisions in the incremental history</translation>
            </message>
            <message>
                <source>文档脚本（JavaScript）</source>
                <translation>Document scripts (JavaScript)</translation>
            </message>
            <message>
                <source>自动动作（打开时 / 关闭时触发）</source>
                <translation>Automatic actions (run on open / close)</translation>
            </message>
            <message>
                <source>表单字段的已填值</source>
                <translation>Filled-in form field values</translation>
            </message>
            <message>
                <source>页面缩略图</source>
                <translation>Page thumbnails</translation>
            </message>
            <message>
                <source>注释（含隐藏注释）</source>
                <translation>Annotations (including hidden ones)</translation>
            </message>
            <message>
                <source>不可见文字层</source>
                <translation>Invisible text layers</translation>
            </message>
            <message>
                <source>图层（OCG）</source>
                <translation>Layers (OCG)</translation>
            </message>
            <message>
                <source>XFA 表单数据</source>
                <translation>XFA form data</translation>
            </message>
            <message>
                <source>数字签名</source>
                <translation>Digital signatures</translation>
            </message>
            <message>
                <source>文件附件的引用点</source>
                <translation>File attachment references</translation>
            </message>
            <message>
                <source>嵌入文件本体</source>
                <translation>Embedded file payloads</translation>
            </message>
                <message>
                <source>勾掉要清理的类别。默认只清文档属性、元数据流、旧版本、脚本、自动动作、批注动作和缩略图；签名、表单已填值、不可见文字层、图层与嵌入文件默认不勾，勾了才会删。</source>
                <translation>Uncheck the categories to clean. By default only document properties, metadata streams, old revisions, scripts, automatic actions, annotation actions and thumbnails are cleared; signatures, filled form values, invisible text layers, layers and embedded files are unchecked and are only removed if you check them.</translation>
            </message>
</context>
    <context>
        <name>aipdf::app::HiddenDataDialog</name>
            <message>
                <source>批注上的自动动作（进入 / 点开时触发）</source>
                <translation>Automatic actions on annotations (on enter / on click)</translation>
            </message>
            <message>
                <source>表单域上的自动动作（校验 / 格式化脚本）</source>
                <translation>Automatic actions on form fields (validation / formatting scripts)</translation>
            </message>
    </context>
    <context>
        <name>aipdf::app::PdfView</name>
            <message>
                <source>需要密码</source>
                <translation>Password required</translation>
            </message>
            <message>
                <source>这份文档已加密，请输入打开密码：</source>
                <translation>This document is encrypted. Enter the password to open it:</translation>
            </message>
            <message>
                <source>这份文档已加密，需要正确的打开密码才能检查和清理隐藏信息。</source>
                <translation>This document is encrypted. The correct password is required to inspect and clean hidden data.</translation>
            </message>
    </context>
    <context>
        <name>aipdf::pdf::DocumentWriter</name>
            <message>
                <source>文档已加密，但没有它的打开密码，无法改加密设置</source>
                <translation>The document is encrypted and its opening password is unknown, so the encryption settings cannot be changed</translation>
            </message>
            <message>
                <source>文档已加密，但没有它的打开密码，无法保存</source>
                <translation>The document is encrypted and its opening password is unknown, so it cannot be saved</translation>
            </message>
    </context>
    <context>
        <name>aipdf::app::PdfView</name>
        <message>
            <source>源文件打开密码（目录里有加密文件时填）</source>
            <translation>Source file password (fill it in when the folder contains encrypted files)</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::BookmarkTreeDialog</name>
        <message>
            <source>编辑书签层级</source>
            <translation>Edit bookmark hierarchy</translation>
        </message>
        <message>
            <source>拖动书签可以改层级：拖到条目中间变成它的子节点，拖到条目上缘或下缘排在它前面或后面，拖到空白处放到顶层。</source>
            <translation>Drag bookmarks to change the hierarchy: drop on the middle of an item to make it a child of that item, on its top or bottom edge to place it before or after it, and on empty space to move it to the top level.</translation>
        </message>
        <message>
            <source>书签</source>
            <translation>Bookmark</translation>
        </message>
        <message>
            <source>跳转到</source>
            <translation>Go to</translation>
        </message>
        <message>
            <source>把书签拖到新的位置即可改层级。</source>
            <translation>Drag a bookmark to a new position to change the hierarchy.</translation>
        </message>
        <message>
            <source>应用</source>
            <translation>Apply</translation>
        </message>
        <message>
            <source>取消</source>
            <translation>Cancel</translation>
        </message>
        <message>
            <source>第 %1 页</source>
            <translation>Page %1</translation>
        </message>
        <message>
            <source>没有目标</source>
            <translation>No target</translation>
        </message>
        <message>
            <source>放到顶层</source>
            <translation>Move to top level</translation>
        </message>
        <message>
            <source>变成「%1」的子节点</source>
            <translation>Make it a child of “%1”</translation>
        </message>
        <message>
            <source>排在「%1」前面</source>
            <translation>Place it before “%1”</translation>
        </message>
        <message>
            <source>排在「%1」后面</source>
            <translation>Place it after “%1”</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::StampNumberDialog</name>
        <message>
            <source>编号图章</source>
            <translation>Number stamp</translation>
        </message>
        <message>
            <source>编号形式:</source>
            <translation>Number style:</translation>
        </message>
        <message>
            <source>数字（1、2、3）</source>
            <translation>Digits (1, 2, 3)</translation>
        </message>
        <message>
            <source>大写字母（A、B、C）</source>
            <translation>Upper case letters (A, B, C)</translation>
        </message>
        <message>
            <source>小写字母（a、b、c）</source>
            <translation>Lower case letters (a, b, c)</translation>
        </message>
        <message>
            <source>起始序号:</source>
            <translation>Start number:</translation>
        </message>
        <message>
            <source>补零位数:</source>
            <translation>Leading zeros:</translation>
        </message>
        <message>
            <source>前缀:</source>
            <translation>Prefix:</translation>
        </message>
        <message>
            <source>后缀:</source>
            <translation>Suffix:</translation>
        </message>
        <message>
            <source>第一个图章: %1</source>
            <translation>First stamp: %1</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::ReadingOptionsDialog</name>
        <message>
            <source>朗读设置</source>
            <translation>Narration settings</translation>
        </message>
        <message>
            <source>音色:</source>
            <translation>Voice:</translation>
        </message>
        <message>
            <source>自动（按文本语言选择）</source>
            <translation>Automatic (pick by the text language)</translation>
        </message>
        <message>
            <source>%1（%2）</source>
            <translation>%1 (%2)</translation>
        </message>
        <message>
            <source>语速:</source>
            <translation>Rate:</translation>
        </message>
        <message>
            <source>音量:</source>
            <translation>Volume:</translation>
        </message>
        <message>
            <source>当前语速: %1</source>
            <translation>Current rate: %1</translation>
        </message>
        <message>
            <source>当前音量: %1</source>
            <translation>Current volume: %1</translation>
        </message>
        <message>
            <source>以下内容按页面上的视觉顺序排列（多栏与表格已按位置重排），可用于人工核对屏幕阅读器的阅读次序。</source>
            <translation>The content is listed in visual order on the page (multi-column layouts and tables are re-ordered), so you can check the reading order a screen reader will use.</translation>
        </message>
        <message>
            <source>当前页阅读顺序</source>
            <translation>Reading order of the current page</translation>
        </message>
        <message>
            <source>阅读顺序</source>
            <translation>Reading order</translation>
        </message>
        <message>
            <source>阅读选项</source>
            <translation>Reading options</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::RegionPicker</name>
        <message>
            <source>选择截图区域</source>
            <translation>Select screenshot region</translation>
        </message>
        <message>
            <source>拖动鼠标框选要截图的区域，松开鼠标确认，Esc 取消</source>
            <translation>Drag to select the screenshot region, release to confirm, Esc to cancel</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::PdfView</name>
        <message>
            <source>复制</source>
            <translation>Copy</translation>
        </message>
        <message>
            <source>全选当前页</source>
            <translation>Select All on This Page</translation>
        </message>
        <message>
            <source>后退</source>
            <translation>Back</translation>
        </message>
        <message>
            <source>前进</source>
            <translation>Forward</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::MainWindow</name>
        <message>
            <source>%1 已经打开了</source>
            <translation>%1 is already open</translation>
        </message>
        <message>
            <source>上一张图片还在生成，请等它完成再试。</source>
            <translation>The previous image is still being generated; please wait for it to finish.</translation>
        </message>
        <message>
            <source>签名验证进程启动失败。</source>
            <translation>The signature verification process could not be started.</translation>
        </message>
        <message>
            <source>签名验证进程超时。</source>
            <translation>The signature verification process timed out.</translation>
        </message>
    </context>
    <context>
        <name>aipdf::app::PdfView</name>
        <message>
            <source>保存完成，但有提示</source>
            <translation>Saved, but with a notice</translation>
        </message>
        <message>
            <source>批注外观流没有写入：在其它阅读器里可能看不见批注</source>
            <translation>Annotation appearance streams were not written; annotations may be invisible in other readers</translation>
        </message>
        <message>
            <source>%1

文档本身已经保存成功；批注在本程序里正常，但在福昕、Acrobat 等阅读器里可能显示不出来。</source>
            <translation>%1

The document itself was saved successfully. Annotations look fine in this app, but may not appear in readers such as Foxit or Acrobat.</translation>
        </message>
        <message>
            <source>文档包含数字签名，本次保存已使签名失效</source>
            <translation>The document contains digital signatures; saving has invalidated them</translation>
        </message>
        <message>
            <source>数字签名已失效</source>
            <translation>Digital signatures invalidated</translation>
        </message>
        <message>
            <source>这份文档包含 %1 个数字签名。保存会重写整份文件，签名覆盖的内容已经改变，在阅读器里会被判定为无效。

如果需要保留签名，请在签署完成后不要再次修改并保存这份文件。</source>
            <translation>This document contains %1 digital signature(s). Saving rewrites the whole file, so the signed content has changed and readers will report the signatures as invalid.

If you need to keep the signatures, do not modify and save this file again after signing.</translation>
        </message>
        <message>
            <source>第 %1 页读不出来，没有插入任何页面。</source>
            <translation>Page %1 could not be read; no pages were inserted.</translation>
        </message>
        <message>
            <source>插入失败，文档已恢复到操作前。</source>
            <translation>Insert failed; the document was restored to its previous state.</translation>
        </message>
        <message>
            <source>第 %1 页渲染失败：页面内容可能已损坏</source>
            <translation>Page %1 failed to render; its content may be damaged</translation>
        </message>
    </context>
    <context>
        <name>aipdf::pdf::IncrementalWriter</name>
        <message>
            <source>文档已加密：增量写入不支持加密文件</source>
            <translation>The document is encrypted; incremental writing does not support encrypted files</translation>
        </message>
    </context>
    <context>
        <name>aipdf::pdf::PdfiumAdapter</name>
        <message>
            <source>无法读取新的文档字节</source>
            <translation>Cannot read the new document bytes</translation>
        </message>
    </context>
<context>
    <name>aipdf::app::ThreeDViewerDialog</name>
    <message><source>3D 模型查看</source><translation>3D model viewer</translation></message>
    <message><source>渲染失败：%1</source><translation>Rendering failed: %1</translation></message>
    <message><source>重置视角</source><translation>Reset view</translation></message>
    <message><source>关闭</source><translation>Close</translation></message>
    <message><source>3D 模型（自研解码，可旋转）</source><translation>3D model (decoded in-house, rotatable)</translation></message>
    <message><source>降级海报（非 3D 模型）</source><translation>Fallback poster (not a 3D model)</translation></message>
    <message><source>SVG 回退表示（非 3D 模型）</source><translation>SVG fallback representation (not a 3D model)</translation></message>
    <message><source>模型类型：%1；网格 %2 个；顶点 %3；三角形 %4；方位角 %5°，俯仰角 %6°，缩放 %7×</source><translation>Model type: %1; meshes %2; vertices %3; triangles %4; azimuth %5°, elevation %6°, zoom %7×</translation></message>
</context>
<context>
    <name>aipdf::pdf::StreamingEdit</name>
    <message><source>第 %1 页的内容流解析不了：%2</source><translation>Cannot parse the content stream on page %1: %2</translation></message>
    <message><source>写回后的内容流解析不了：%1</source><translation>Cannot parse the content stream after writing back: %1</translation></message>
    <message><source>这一段按新宽度一行都排不出来</source><translation>Not a single line of this paragraph fits the new width</translation></message>
    <message><source>这一段按新宽度排不完，有文字剩下</source><translation>This paragraph does not fit the new width; some text is left over</translation></message>
    <message><source>PDFium 打不开这份 PDF</source><translation>PDFium cannot open this PDF</translation></message>
    <message><source>内容流通道不可用：这一版没有编进 qpdf</source><translation>The content-stream channel is unavailable: this build has no qpdf</translation></message>
    <message><source>输入的 PDF 是空的</source><translation>The input PDF is empty</translation></message>
    <message><source>第 %1 页上找不到要改写的文字块</source><translation>Cannot find the text block to rewrite on page %1</translation></message>
    <message><source>没有需要写回的内容</source><translation>There is nothing to write back</translation></message>
    <message><source>写回的 PDF 是空的</source><translation>The written PDF is empty</translation></message>
    <message><source>目标页宽必须大于 0</source><translation>The target page width must be greater than 0</translation></message>
    <message><source>左右边距把可用宽度吃光了，没有可排版的栏宽</source><translation>The left and right margins leave no usable column width</translation></message>
    <message><source>这份文档没有结构标签，没有可靠的段落边界，整篇流式重排已拒绝；只能逐段处理</source><translation>This document has no structure tags, so it has no reliable paragraph boundaries; whole-document streaming reflow was refused and only per-paragraph handling is possible</translation></message>
    <message><source>这份文档没有结构标签，本次只按内容流里的段落逐段处理，不算整篇流式重排</source><translation>This document has no structure tags; this run only handles the paragraphs in the content stream and is not a whole-document streaming reflow</translation></message>
    <message><source>这一段的文字与别的内容交错，没法整段换行，保留原文</source><translation>This paragraph's text interleaves with other content and cannot be rewrapped as a whole; the original text is kept</translation></message>
    <message><source>这一段按新宽度排不出来，保留原文</source><translation>This paragraph cannot be laid out at the new width; the original text is kept</translation></message>
    <message><source>有 %1 段文字里有标准 14 号字体表示不了的字符，写回时写成了 '?' 占位符</source><translation>In %1 paragraph(s) some characters cannot be represented by the standard 14 fonts; they were written back as '?' placeholders</translation></message>
    <message><source>没有一段能按新宽度重排，文档没有改动</source><translation>No paragraph could be reflowed at the new width; the document is unchanged</translation></message>
    <message><source>写回后打不开：%1</source><translation>Cannot open the document after writing back: %1</translation></message>
    <message><source>写回后页数变了（原来 %1 页，现在 %2 页）</source><translation>The page count changed after writing back (was %1 pages, now %2 pages)</translation></message>
    <message><source>写回后第 %1 页上有一段文字少了字，已放弃写回</source><translation>A paragraph on page %1 lost characters after writing back; the write-back was abandoned</translation></message>
    <message><source>至少要两个文本块才谈得上合并，现在只有 %1 个</source><translation>Merging needs at least two text blocks; there is only %1</translation></message>
    <message><source>找不到编号为 %1 的文本块</source><translation>Cannot find the text block numbered %1</translation></message>
    <message><source>选区跨了页，合并之后没法放在同一页上，已拒绝</source><translation>The selection spans pages and cannot be merged onto one page; refused</translation></message>
    <message><source>选中的块之间夹着图片或表格，合并会把中间的内容挤掉，已拒绝</source><translation>Images or tables sit between the selected blocks and merging would squeeze them out; refused</translation></message>
    <message><source>合并之后一段都排不出来</source><translation>Not one paragraph could be laid out after merging</translation></message>
    <message><source>合并之后超出了页面下边界，文字仍然写进 PDF，但会印到页面外面</source><translation>After merging the text goes past the bottom edge of the page; it is still written into the PDF but will print outside the page</translation></message>
    <message><source>写回后页数变了</source><translation>The page count changed after writing back</translation></message>
    <message><source>写回后字数少了（合并前 %1 个字，写回后 %2 个字），已放弃写回</source><translation>The character count dropped after writing back (was %1 before merging, %2 after); the write-back was abandoned</translation></message>
    <message><source>合并后的文字没有完整落到页面上，已放弃写回</source><translation>The merged text did not fully land on the page; the write-back was abandoned</translation></message>
    <message><source>这一段按新宽度排下来放不进原来的位置（会压到下一段或超出页面），保留原文</source><translation>This paragraph does not fit in its original position at the new width (it would overlap the next paragraph or run past the page); the original text is kept</translation></message>
</context>
<context>
    <name>aipdf::pdf::TranslationWriter</name>
    <message><source>输入的 PDF 是空的</source><translation>The input PDF is empty</translation></message>
    <message><source>没有翻译 Provider，这一篇没法翻译</source><translation>There is no translation provider, so this document cannot be translated</translation></message>
    <message><source>翻译 Provider 是演示模式，演示出来的示例不能写回文档</source><translation>The translation provider is in demo mode; demo output cannot be written back to the document</translation></message>
    <message><source>翻译 Provider 调用失败</source><translation>The translation provider call failed</translation></message>
    <message><source>翻译 Provider 返回的是空内容，没有译文</source><translation>The translation provider returned empty content; there is no translation</translation></message>
    <message><source>翻译 Provider 返回的内容与原文一样，不像是译文，这一段保留原文</source><translation>The translation provider returned the same text as the source; it does not look like a translation, so the original text is kept</translation></message>
    <message><source>译文里有当前字体表示不了的字符（要写中文译文得先嵌入中文字体），这一段保留原文</source><translation>The translation contains characters the current font cannot represent (writing Chinese translations requires embedding a CJK font first); the original text is kept</translation></message>
    <message><source>译文排不出来，这一段保留原文</source><translation>The translation cannot be laid out; the original text is kept</translation></message>
    <message><source>第 %1 页上有一段译文超出了原来的位置</source><translation>A translation on page %1 extends beyond its original position</translation></message>
    <message><source>没有一段译文可以写回，文档保持原样</source><translation>There is no translation to write back; the document stays as it is</translation></message>
    <message><source>写回后打不开：%1</source><translation>Cannot open the document after writing back: %1</translation></message>
    <message><source>写回后页数变了（原来 %1 页，现在 %2 页）</source><translation>The page count changed after writing back (was %1 pages, now %2 pages)</translation></message>
    <message><source>第 %1 页上第 %2 段译文没有落到 PDF 里，已放弃写回</source><translation>Translation %2 on page %1 did not land in the PDF; the write-back was abandoned</translation></message>
    <message><source>第 %1 页上第 %2 段的原文不见了，已放弃写回</source><translation>The original text of paragraph %2 on page %1 is missing; the write-back was abandoned</translation></message>
    <message><source>译文比原文长，原来的位置放不下（会压到下一段或超出页面），这一段保留原文</source><translation>The translation is longer than the source and does not fit in its original position (it would overlap the next paragraph or run past the page); the original text is kept</translation></message>
</context>
<context>
    <name>aipdf::app::TextBlockMergeDialog</name>
    <message><source>跨文本块合并</source><translation>Merge across text blocks</translation></message>
    <message><source>勾选要合并成一段的文本块（至少两块）。块与块之间夹着图片或表格时会被拒绝。</source><translation>Tick the text blocks to merge into one paragraph (at least two). Merging is refused when images or tables sit between the blocks.</translation></message>
    <message><source>块 %1：%2（位置 %3, %4）</source><translation>Block %1: %2 (position %3, %4)</translation></message>
    <message><source>（空）</source><translation>(empty)</translation></message>
    <message><source>合并</source><translation>Merge</translation></message>
    <message><source>取消</source><translation>Cancel</translation></message>
</context>
</TS>
