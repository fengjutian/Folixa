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
            <source>密钥保存在 Windows 凭据管理器里，不写进配置文件。</source>
            <translation>The key is stored in Windows Credential Manager, not in the configuration file.</translation>
        </message>
        <message>
            <source>数据模式</source>
            <translation>Data mode</translation>
        </message>
        <message>
            <source>文档内容一律不出本机；即使配了密钥也只用本地演示模型。</source>
            <translation>Document content never leaves this device; even with a key, only the local demo model is used.</translation>
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
    </context>
    <context>
        <name>aipdf::app::PdfView</name>
        <message>
            <source>已用 %1 的第 %2 页替换第 %3 页（可撤销）</source>
            <translation>Replaced page %3 with page %2 of %1 (undoable)</translation>
        </message>

        <message>
            <source>提取页面</source>
            <translation>Extract pages</translation>
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
</TS>
