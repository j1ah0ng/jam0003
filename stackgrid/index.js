const readlineSync = require('readline-sync'); // disable in prod

class Token {
  constructor(tokenType, lexeme, literal) {
    this.tokenType = tokenType;
    this.lexeme = lexeme;
    this.literal = literal;
  }
}

const TokenType = {
  NewLine: 'NEW_LINE',
  NewColumn: 'NEW_COLUMN',
  Identifier: 'IDENTIFIER',
  Number: 'NUMBER',
  Eof: 'EOF',
};

class Scanner {
  /**
   *
   * @param {string} source
   */
  constructor(source) {
    this.source = source;
    this.start = 0;
    this.current = 0;

    /**
     * @type {Token[]}
     */
    this.tokens = [];
  }

  scanTokens() {
    while (!this.isAtEnd()) {
      this.start = this.current;
      this.scanToken();
    }

    this.addToken(TokenType.Eof, null);

    return this.tokens;
  }

  scanToken() {
    const char = this.advance();

    switch (char) {
      case '/':
        if (this.peek() === '*') {
          while (!(this.peek() === '*' && this.peekNext() === '/')) {
            this.advance();
          }
          this.advance();
          this.advance();
          break;
        }
      // TODO: test the edge case of slash starting program but not comment, even though should fail parsing error
      case ',':
        this.addToken(TokenType.NewColumn, null);
        // TODO: multiple separators
        break;
      case ' ':
        break;
      case '\n':
        this.addToken(TokenType.NewLine, null);
        break;
      default:
        if (this.isDigit(char)) {
          this.number();
        } else if (this.isAlpha(char)) {
          this.identifier();
        } else {
          throw new Error('Unexpected character:' + char);
        }
        break;
    }
  }

  number() {
    while (this.isDigit(this.peek())) {
      this.advance();
    }

    // look for fractional part
    if (this.peek() === '.' && this.isDigit(this.peekNext())) {
      this.advance();
      while (this.isDigit(this.peek())) {
        this.advance();
      }
    }

    const value = parseFloat(this.source.slice(this.start, this.current));
    this.addToken(TokenType.Number, value);
  }

  identifier() {
    while (this.isAlphaNumeric(this.peek())) {
      this.advance();
    }

    this.addToken(TokenType.Identifier);
  }

  addToken(tokenType, literal) {
    const text = this.source.slice(this.start, this.current);
    this.tokens.push(new Token(tokenType, text, literal));
  }

  peekNext() {
    if (this.current + 1 >= this.source.length) {
      return '\\000';
    }
    return this.source[this.current + 1];
  }

  advance() {
    const current = this.source[this.current];
    this.current++;
    return current;
  }

  isAtEnd() {
    return this.current >= this.source.length;
  }

  peek() {
    if (this.isAtEnd()) {
      return '\\000';
    }
    return this.source[this.current];
  }

  isAlphaNumeric(char) {
    return this.isAlpha(char) || this.isDigit(char);
  }

  isDigit(char) {
    return char >= '0' && char <= '9';
  }

  isAlpha(char) {
    return (char >= 'a' && char <= 'z') || (char >= 'A' && char <= 'Z') || char == '_';
  }
}

class Row {
  /**
   *
   * @param {number} index
   * @param {Cell[]} cells
   */
  constructor(index, cells) {
    this.index = index;
    this.cells = cells;
  }
}

class Cell {
  /**
   *
   * @param {number} index
   * @param {number} rowIndex
   * @param {Value} instruction // TODO: This should probably just be a Stmt or similar
   */
  constructor(index, rowIndex, instruction) {
    this.index = index;
    this.rowIndex = rowIndex;
    this.instruction = instruction;
  }
}

class Parser {
  constructor(tokens) {
    this.tokens = tokens;
    this.current = 0;
    /**
     * @type {Row[]}
     */
    this.rows = [];
    this.row = 0;
    this.column = 0;
  }

  parse() {
    this.rows.push(new Row(this.row, [new Cell(this.row, this.column, new Value(''))]));

    while (!this.isAtEnd()) {
      if (this.match(TokenType.NewLine)) {
        this.column = 0;
        this.rows.push(new Row(this.row++, [new Cell(this.column, this.row, new Value(''))]));
      } else if (this.match(TokenType.NewColumn)) {
        const currentRow = this.rows[this.rows.length - 1];
        currentRow.cells.push(new Cell(++this.column, this.row, new Value('')));
      } else {
        const currentRow = this.rows[this.rows.length - 1];
        const currentCell = currentRow.cells[currentRow.cells.length - 1];
        currentCell.instruction.value = [currentCell.instruction.value, this.advance().lexeme].join(' ').trim();
      }
    }

    return this.rows;
  }

  match(...tokenTypes) {
    for (const tokenType of tokenTypes) {
      if (this.check(tokenType)) {
        this.advance();
        return true;
      }
    }

    return false;
  }

  check(tokenType) {
    if (this.isAtEnd()) {
      return false;
    }
    return this.peek().tokenType == tokenType;
  }

  peek() {
    return this.tokens[this.current];
  }

  advance() {
    if (!this.isAtEnd()) {
      this.current++;
    }
    return this.previous();
  }

  previous() {
    return this.tokens[this.current - 1];
  }

  isAtEnd() {
    return this.peek().tokenType == TokenType.Eof;
  }
}

// TODO: I guess eventually maybe everything is a number, instruction and data...
// and the interpreter has an assembler stage, maybe during/before parsing

class Value {
  // TODO: May have to put the line numbers and all inside the values
  /**
   *
   * @param {string} value
   */
  constructor(value) {
    this.value = value;
  }
}

class Interpreter {
  /**
   *
   * @param {Row[]} rows
   */
  constructor(rows) {
    this.rows = rows;
    /**
     * @type {(Value | null)[][]}
     */
    this.state = [];
    this.currentRow = 0;
    this.currentColumn = 0;
  }

  interpret() {
    this.state = Array.from(new Array(20), () => Array.from(new Array(5), () => null));

    for (let i = 0; i < this.rows.length; i++) {
      const row = this.rows[i];
      for (let j = 0; j < row.cells.length; j++) {
        const element = row.cells[j];
        this.state[element.rowIndex][element.index] = element.instruction;
      }
    }

    this.debug();

    program: while (true) {
      const next = this.advance();
      if (next === null || next.value === '') {
        continue;
      }

      const [operator, ...operands] = next.value.split(' ');

      switch (operator) {
        case this.operators.JUMP_IF_STACK_EMPTY: {
          const [stack, target] = operands;

          const isStackEmpty = this.isStackEmpty(stack);
          if (!isStackEmpty) {
            break;
          }

          const [row, column] = this.getPosition(target);
          this.currentRow = row;
          this.currentColumn = column;
          break;
        }
        case this.operators.PRINTASCII: {
          const [target] = operands;
          const [row, column] = this.getPosition(target);
          const value = this.pop(row, column);
          this.print(value, true);
          break;
        }
        case this.operators.PRINT: {
          const [target] = operands;
          const [row, column] = this.getPosition(target);
          const value = this.pop(row, column);
          this.print(value, false);
          break;
        }
        case this.operators.JUMP: {
          const [target] = operands;
          const [row, column] = this.getPosition(target);
          this.currentRow = row;
          this.currentColumn = column;
          break;
        }
        case this.operators.READASCII: {
          const [target] = operands;
          const [row, column] = this.getPosition(target);

          const input = readlineSync.question('');
          this.push(row, column, '\n'.charCodeAt(0).toString());
          for (let i = input.length - 1; i >= 0; i--) {
            this.push(row, column, input.charCodeAt(i).toString());
          }

          break;
        }
        case this.operators.DUP: {
          const [stack] = operands;
          const [row, column] = this.getPosition(stack);
          const value = this.peek(row, column);
          this.push(row, column, value.value);
          break;
        }
        case this.operators.CYCLE: {
          const [stack] = operands;
          const [row, column] = this.getPosition(stack);
          this.cycle(row, column);
          break;
        }
        case this.operators.ADD:
        case this.operators.SUB:
        case this.operators.MUL:
        case this.operators.DIV:
        case this.operators.MOD:
        case this.operators.AND:
        case this.operators.OR:
        case this.operators.XOR:
        case this.operators.NAND:
        case this.operators.NOT: {
          const [stack] = operands;
          const [row, column] = this.getPosition(stack);
          // TODO: recheck pop positions
          const op2 = this.pop(row, column);
          const op1 = this.pop(row, column);

          // TODO: Fill in
          let value;
          switch (operator) {
            case this.operators.ADD:
              value = (parseInt(op1.value) + parseInt(op2.value)).toString();
              break;
            default:
              throw new Error('unknown operator');
          }

          this.push(row, column, value);
          break;
        }
        case this.operators.JEQ: {
          const [stack1, stack2, target] = operands;

          const [row1, column1] = this.getPosition(stack1);
          const head1 = this.peek(row1, column1);

          const [row2, column2] = this.getPosition(stack2);
          const head2 = this.peek(row2, column2);

          if (head1.value === head2.value) {
            const [targetRow, targetColumn] = this.getPosition(target);
            this.currentRow = targetRow;
            this.currentColumn = targetColumn;
            break;
          }
          break;
        }
        case this.operators.INC: {
          const [stack] = operands;
          const [row, column] = this.getPosition(stack);
          const value = this.pop(row, column);
          this.push(row, column, (parseInt(value.value) + 1).toString());
          break;
        }
        case this.operators.EXIT: {
          break program;
        }
        default:
          throw new Error('unhandled interpret: ' + operator);
      }
    }
  }

  c = 2;

  operators = {
    JUMP: 'JUMP', // JUMP [target]
    JUMP_IF_STACK_EMPTY: 'JSE', // JSE [stack] [target]
    JEQ: 'JEQ', // JEQ [stack 1] [stack 2] [target]

    READASCII: 'READASCII', // READASCII [stack]
    PRINTASCII: 'PRINTASCII', // PRINTASCII [stack]

    READ: 'READ',
    PRINT: 'PRINT', // PRINT [stack]

    EXIT: 'EXIT', // EXIT

    // TODO: implement pushing from one stack to another, maybe MOVE
    PUSH: 'PUSH',
    CYCLE: 'CYCLE',
    INC: 'INC',

    // TODO: Change instruction pointer direction
    ROTATE: 'ROTATE', // rotate the instruction pointer direction clockwise

    // TODO: Arithmetic operations
    ADD: 'ADD',
    SUB: 'SUB',
    MUL: 'MUL',
    DIV: 'DIV',
    MOD: 'MOD',

    AND: 'AND',
    OR: 'OR',
    XOR: 'XOR',
    NAND: 'NAND',
    NOT: 'NOT',

    DUP: 'DUP',
  };

  advance() {
    if (this.currentRow >= this.state.length) {
      return null;
    }
    return this.state[this.currentRow++][this.currentColumn];
  }

  getPosition(address) {
    const [column, row] = address.split('');
    return [parseInt(row) - 1, column.charCodeAt(0) - 'A'.charCodeAt(0)];
  }

  /**
   *
   * @param {Value} value
   * @param {boolean} isAscii
   */
  print(value, isAscii = false) {
    let toPrint = value.value;
    if (isAscii) {
      toPrint = String.fromCharCode(parseInt(toPrint));
    } else {
      toPrint += ' ';
    }
    process.stdout.write(toPrint);
  }

  /**
   *
   * @param {number} stackRow
   * @param {number} stackColumn
   * @param {string} value
   */
  push(stackRow, stackColumn, value) {
    let head = this.state[stackRow][stackColumn];
    if (head === null || head.value.length === 0) {
      this.state[stackRow][stackColumn] = new Value(value);
      return;
    }

    while (true) {
      const next = this.state[stackRow + 1][stackColumn];
      if (next === null || next.value.length === 0) {
        break;
      }

      stackRow++;
      head = next;
    }

    this.state[stackRow + 1][stackColumn] = new Value(value);
  }

  peek(stackRow, stackColumn) {
    let head = this.state[stackRow][stackColumn];
    while (true) {
      const next = this.state[stackRow + 1][stackColumn];
      if (next === null || next.value.length === 0) {
        break;
      }

      stackRow++;
      head = next;
    }
    return head;
  }

  /**
   *
   * @param {number} stackRow
   * @param {number} stackColumn
   * @returns
   */
  pop(stackRow, stackColumn) {
    let head = this.state[stackRow][stackColumn];
    while (true) {
      const next = this.state[stackRow + 1][stackColumn];
      if (next === null || next.value.length === 0) {
        break;
      }

      stackRow++;
      head = next;
    }

    const value = new Value(head.value);
    this.state[stackRow][stackColumn] = new Value('');
    return value;
  }

  cycle(stackRow, stackColumn) {
    let head = this.state[stackRow][stackColumn];
    if (head === null || head.value.length === 0) {
      return;
    }

    let row = stackRow;
    let column = stackColumn;
    while (true) {
      const next = this.state[row + 1][column];
      if (next === null || next.value.length === 0) {
        break;
      }

      row++;
      head = next;
    }

    // has to be a more performant way to do this

    const stackCopy = [];
    for (let i = stackRow; i <= row; i++) {
      stackCopy.push(new Value(this.state[i][column].value));
    }

    for (let i = stackRow; i <= row; i++) {
      this.state[i][stackColumn] = new Value(stackCopy[(i + 1) % stackCopy.length].value);
    }
  }

  isStackEmpty(stack) {
    const [row, column] = this.getPosition(stack);
    const cell = this.state[row][column];
    return cell === undefined || cell === null || cell.value.length === 0;
  }

  debug() {
    console.log(this.state.map((row) => row.map((cell) => (cell === null ? null : cell.value))));
  }
}

function run(source) {
  const scanner = new Scanner(source);
  const tokens = scanner.scanTokens();

  const parser = new Parser(tokens);
  const statements = parser.parse();

  const interpreter = new Interpreter(statements);
  interpreter.interpret();
}

module.exports = { run };
