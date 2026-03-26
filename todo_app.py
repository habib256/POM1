#!/usr/bin/env python3
"""A simple command-line TODO application with file persistence."""

import json
import os
import sys

TODO_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "todos.json")


def load_todos():
    if os.path.exists(TODO_FILE):
        with open(TODO_FILE, "r") as f:
            return json.load(f)
    return []


def save_todos(todos):
    with open(TODO_FILE, "w") as f:
        json.dump(todos, f, indent=2)


def list_todos(todos):
    if not todos:
        print("No todos. Add one with: todo add <task>")
        return
    for i, todo in enumerate(todos, 1):
        status = "x" if todo["done"] else " "
        print(f"  [{status}] {i}. {todo['task']}")


def add_todo(todos, task):
    todos.append({"task": task, "done": False})
    save_todos(todos)
    print(f"Added: {task}")


def done_todo(todos, index):
    if 1 <= index <= len(todos):
        todos[index - 1]["done"] = True
        save_todos(todos)
        print(f"Done: {todos[index - 1]['task']}")
    else:
        print(f"Invalid index: {index}")


def undone_todo(todos, index):
    if 1 <= index <= len(todos):
        todos[index - 1]["done"] = False
        save_todos(todos)
        print(f"Undone: {todos[index - 1]['task']}")
    else:
        print(f"Invalid index: {index}")


def remove_todo(todos, index):
    if 1 <= index <= len(todos):
        removed = todos.pop(index - 1)
        save_todos(todos)
        print(f"Removed: {removed['task']}")
    else:
        print(f"Invalid index: {index}")


def print_usage():
    print("Usage: todo <command> [args]")
    print()
    print("Commands:")
    print("  list            Show all todos")
    print("  add <task>      Add a new todo")
    print("  done <number>   Mark a todo as done")
    print("  undone <number> Mark a todo as not done")
    print("  remove <number> Remove a todo")


def main():
    args = sys.argv[1:]
    if not args:
        todos = load_todos()
        list_todos(todos)
        return

    command = args[0]
    todos = load_todos()

    if command == "list":
        list_todos(todos)
    elif command == "add" and len(args) > 1:
        add_todo(todos, " ".join(args[1:]))
    elif command == "done" and len(args) == 2:
        done_todo(todos, int(args[1]))
    elif command == "undone" and len(args) == 2:
        undone_todo(todos, int(args[1]))
    elif command == "remove" and len(args) == 2:
        remove_todo(todos, int(args[1]))
    else:
        print_usage()


if __name__ == "__main__":
    main()
