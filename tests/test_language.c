#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "orion/language.h"
#include "orion/lexer.h"

int main(void) {
    const char *source =
        "fn twoSum(int* nums, int n) -> int {"
        "  int* answer = alloc int[2]; int i = 0;"
        "  while (i < n) { int j = i + 1;"
        "    while (j < n) {"
        "      if (nums[i] + nums[j] == 9) {"
        "        answer[0] = i; answer[1] = j; return answer;"
        "      } j = j + 1;"
        "    } i = i + 1;"
        "  } return null;"
        "}"
        "fn main() -> int {"
        "  int* nums = alloc int[4];"
        "  nums[0] = 2; nums[1] = 7; nums[2] = 11; nums[3] = 15;"
        "  int* answer = twoSum(nums, 4);"
        "  int packed = answer[0] * 10 + answer[1];"
        "  print(packed); free(answer); free(nums); return packed;"
        "}";
    TokenStream tokens = lex_source(source);
    Parser parser = {tokens.data, 0, tokens.count};
    ASTNode *program = parse_program(&parser);
    int result = run_program(program);
    free_token_stream(&tokens);
    assert(result == 1);

    const char *recursive =
        "fn fib(int n) -> int {"
        "  if (n <= 1) { return n; }"
        "  return fib(n - 1) + fib(n - 2);"
        "}"
        "fn main() -> int { return fib(10); }";
    tokens = lex_source(recursive);
    parser = (Parser){tokens.data, 0, tokens.count};
    program = parse_program(&parser);
    result = run_program(program);
    free_token_stream(&tokens);
    assert(result == 55);

    const char *linked_list =
        "struct Node { int value; Node* next; }"
        "fn reverse(Node* head) -> Node* {"
        "  Node* previous = null; Node* current = head;"
        "  while (current != null) {"
        "    Node* next = current.next; current.next = previous;"
        "    previous = current; current = next;"
        "  } return previous;"
        "}"
        "fn main() -> int {"
        "  Node* a = alloc Node; Node* b = alloc Node; Node* c = alloc Node;"
        "  a.value = 1; b.value = 2; c.value = 3;"
        "  a.next = b; b.next = c; c.next = null;"
        "  Node* head = reverse(a);"
        "  int packed = head.value * 100 + head.next.value * 10 + head.next.next.value;"
        "  free(head.next.next); free(head.next); free(head); return packed;"
        "}";
    tokens = lex_source(linked_list);
    parser = (Parser){tokens.data, 0, tokens.count};
    program = parse_program(&parser);
    result = run_program(program);
    free_token_stream(&tokens);
    assert(result == 321);

    puts("Language tests passed: arrays, recursion, linked-list fields, loops, manual allocation and free");
    return 0;
}
