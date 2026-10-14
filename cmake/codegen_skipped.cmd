@echo off
rem Stand-in for `rexglue codegen` when assets\default.xex is absent: a clone
rem of the repository builds the committed generated\ sources as they are.
rem Put your default.xex in assets\ to regenerate them instead.
echo forza2: assets\default.xex not found - using the committed generated\ code
exit /b 0
