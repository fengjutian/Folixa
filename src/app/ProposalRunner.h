#pragma once

#include "doc/EditCommand.h"
#include "tools/ProposalValidator.h"

#include <QString>
#include <vector>

#include <memory>

namespace aipdf::doc {
class EditContext;
class UndoStack;
}  // namespace aipdf::doc

namespace aipdf::app {

// 把校验过的提案翻译成编辑命令。
//
// 这一层是「提案」与「命令」之间唯一的翻译点：
// 提案里是给人看的页码（从 1 开始），命令要的是从 0 开始的下标；
// 提案里是一组独立操作，命令必须按顺序执行——删多页时尤其要注意顺序，
// 否则第二次删除会落在已经移位的位置上。
class ProposalRunner {
public:
    // 命令的所有权在执行时交给撤销栈，所以用 unique_ptr 的列表。
    using CommandList = std::vector<std::unique_ptr<doc::EditCommand>>;

    // 只翻译，不执行。失败时 error 说明是哪一条、为什么。
    static bool buildCommands(const tools::Proposal& proposal, CommandList* commands,
                              QString* error);

    // 逐条执行并压入撤销栈。返回成功执行的条数；失败时 error 说明原因。
    // 只在用户确认后调用——这一步没有内置的确认，调用方负责。
    // 执行会把命令移交给撤销栈（执行一次就消费掉）。
    static int execute(CommandList* commands, doc::EditContext* context,
                       doc::UndoStack* stack, QString* error);
};

}  // namespace aipdf::app
