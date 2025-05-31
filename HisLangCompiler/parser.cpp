// parser.cpp - MyLang parser implementation
#include "parser.hpp"
#include "utils.hpp"
#include <stdexcept>
#include <iostream>

static int pos = 0;
static std::vector<Token> toks;

static Token dummy_eof_token() {
    return Token{ TokenType::EOFToken, "" };
}

static Token peek() {
    if (pos >= toks.size()) {
#if _DEBUG
        std::cerr << "[ERROR] peek: pos=" << pos << ", toks.size=" << toks.size() << "\n";
#endif
        return dummy_eof_token(); // 不再抛异常，返回 EOF
    }
    auto t = toks[pos];
#if _DEBUG
    std::cerr << "[peek] pos=" << pos << ", token=(" << t.value << ")\n";
#endif
    return t;
}

static Token advance() {
    if (pos >= toks.size()) {
#if _DEBUG
        std::cerr << "[ERROR] advance: pos=" << pos << ", toks.size=" << toks.size() << "\n";
#endif
        return dummy_eof_token(); // 不再抛异常
    }
    auto t = toks[pos++];
#if _DEBUG
    std::cerr << "[advance] pos=" << pos << ", token=(" << t.value << ")\n";
#endif
    return t;
}

// 跳过连续空行
static void skip_newlines() {
    while (peek().type == TokenType::Newline) {
        advance();
    }
}

std::shared_ptr<Statement> parse_statement();

AST parse(const std::vector<Token>& tokens) {
    toks = tokens;
    pos = 0;
    AST ast;

    while (pos < toks.size()) {
        Token current = peek();
        if (current.type == TokenType::EOFToken) break;

        auto stmt = parse_statement();
        if (stmt) {
            ast.statements.push_back(stmt);
        }
        else {
            advance(); // 防止死循环
        }
    }

    return ast;
}

std::vector<std::shared_ptr<Statement>> parse_block() {
    std::vector<std::shared_ptr<Statement>> body;
    int safety_counter = 0;

    while (true) {
        skip_newlines();

        Token current = peek();
        if (current.type == TokenType::Keyword && current.value == "end") {
            advance(); // consume "end"
            break;
        }
        if (current.type == TokenType::EOFToken) {
            throw std::runtime_error("Unexpected end of file inside block.");
        }

        auto stmt = parse_statement();
        if (stmt) {
            body.push_back(stmt);
        }
        else {
            advance();
        }

        // 防止死循环
        if (++safety_counter > 10000) {
            throw std::runtime_error("Too many statements parsed without encountering 'end'");
        }
    }

    return body;
}

std::shared_ptr<Statement> parse_statement() {
    skip_newlines();

    Token tok = peek();

    if (tok.type == TokenType::EOFToken) {
        return nullptr;
    }

    // function definition
    if (tok.type == TokenType::Keyword && tok.value == "function") {
        advance(); // consume 'function'

        Token name = advance();
        Token maybe_param_or_colon = advance();

        std::string param = "";  // 默认为空参数

        Token colon;
        if (maybe_param_or_colon.type == TokenType::Symbol && maybe_param_or_colon.value == ":") {
            // 无参数函数
            colon = maybe_param_or_colon;
        }
        else {
            // 有参数函数，接下来应该是冒号
            param = maybe_param_or_colon.value;
            colon = advance();
            if (colon.value != ":") {
                throw std::runtime_error("Expected ':' after parameter in function definition");
            }
        }

        auto body = parse_block();
        return std::make_shared<FunctionDef>(name.value, param, body);
    }

    // start block
    if (tok.type == TokenType::Keyword && tok.value == "start") {
        advance();
        Token colon = advance();
        if (colon.value != ":") throw std::runtime_error("Expected ':' after start");
        auto body = parse_block();
        return std::make_shared<StartBlock>(body);
    }

    // say
    if (tok.type == TokenType::Keyword && tok.value == "say") {
        advance(); // consume 'say'

        std::vector<std::string> args;
        std::vector<bool> is_vars;
        std::string ending = "\\n"; // default end

        while (true) {
            Token next = peek();
#if _DEBUG
            std::cerr << "[DEBUG] say loop: next=" << next.value << ", type=" << static_cast<int>(next.type) << "\n";
#endif

            if (next.type == TokenType::Keyword && next.value == "end") {
                advance(); // consume 'end'

                Token eq = peek();
                if (eq.type != TokenType::Symbol || eq.value != "=") {
                    throw std::runtime_error("Expected '=' after 'end'");
                }
                advance(); // consume '='

                Token val = peek();
                if (val.type != TokenType::StringLiteral) {
                    throw std::runtime_error("Expected string literal after end=");
                }
                ending = advance().value;

                break; // 结束 say 解析
            }

            if (next.type == TokenType::Newline || next.type == TokenType::EOFToken) {
                advance(); // consume newline/EOF
                break;     // 正常结束 say
            }

            // 处理参数
            if (next.type == TokenType::StringLiteral || next.type == TokenType::Identifier) {
                Token arg = advance();
                args.push_back(arg.value);
                is_vars.push_back(arg.type == TokenType::Identifier);

                // 可选逗号
                Token comma = peek();
                if (comma.type == TokenType::Symbol && comma.value == ",") {
                    advance(); // consume comma
                }
            }
            else {
                throw std::runtime_error("Unexpected token in 'say': " + next.value);
            }
        }

        if (args.size() != is_vars.size()) {
            throw std::runtime_error("Internal error: say args/vars mismatch.");
        }

        return std::make_shared<SayStatement>(args, is_vars, ending);
    }

    // set
    if (tok.type == TokenType::Keyword && tok.value == "set") {
        advance();
        Token var = advance();
        return std::make_shared<SetStatement>(var.value);
    }

    // function call
    if (tok.type == TokenType::Identifier) {
        Token func = advance();
        Token next = peek();
        if (next.type == TokenType::StringLiteral || next.type == TokenType::Identifier) {
            Token arg = advance();
#if _DEBUG
            std::cerr << "[DEBUG] function call arg " << arg.value << " ";
            switch (arg.type) {
            case TokenType::Keyword:        std::cerr << "Keyword    "; break;
            case TokenType::Identifier:     std::cerr << "Identifier "; break;
            case TokenType::StringLiteral:  std::cerr << "String     "; break;
            case TokenType::Newline:        std::cerr << "Newline    "; break;
            case TokenType::EOFToken:       std::cerr << "EOF        "; break;
            case TokenType::Symbol:         std::cerr << "Symbol     "; break;
            }
            std::cerr << std::endl;
#endif
            return std::make_shared<FunctionCall>(func.value, arg.value, arg.type);
        }
        else {
            return std::make_shared<FunctionCall>(func.value, "", TokenType::EOFToken);
        }
    }


    // 未知语句，跳过避免死循环
    advance();
    return nullptr;
}
