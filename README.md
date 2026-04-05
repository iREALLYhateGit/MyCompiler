# MyCompiler project #

## Usage ##
1. **Go to build directory:**
```bash
  cd .\cmake-build-debug\  
```
2. **Run "./MyCompiler --help" to get acquainted with allowed scenarios:**

```bash
  ./MyCompiler --help
```

### Modes ###

There are 2 modes to use:  
1. Single mode (also default mode).  
   - Description:  
     Program takes .txt file with code, builds ast tree,
     builds cfg tree and represents it to user in .dot files
     in specified directories.
   - Input:

     Takes 3 parameters in the specified mandatory order:

       1. Path to `.txt` file, that contains code in custom language syntax.
       2. Path to directory where result `ast_tree` will be stored.
       3. Path to directory where result `cfg_tree` will be stored.
  
   - Example:

        ```bash
        .\MyCompiler ..\input.txt output_ast_trees output_cfg_trees
        ```

2. Multiple files processing mode.

    - Description:  
      Program takes multiple .txt files with code, builds ast tree for each,
      builds cfg tree for each,and represents it to user in .dot files
      in .\output_ast_trees and .\output_cfg_trees.
    - Input:

      Takes multiple parameters in the specified mandatory order:

        1. "--multiple" option.
        2. Paths to `.txt` files, that contain code in custom language syntax.

    - Example:

         ```bash
         .\MyCompiler --multiple ..\ast_test_cases\fibonacci.txt ..\ast_test_cases\calc.txt
         ```