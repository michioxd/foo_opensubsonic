## fb2k_example
A minimal project for developing foobar2000 component in VS2022

[![GithubActions_badge]][GithubActions_link]

[GithubActions_badge]: https://github.com/Chocobo1/fb2k_example/actions/workflows/ci.yaml/badge.svg
[GithubActions_link]: https://github.com/Chocobo1/fb2k_example/actions

### Usage
1. Get [foobar2000 SDK](https://www.foobar2000.org/SDK) and extract it into `lib\foobar2000_sdk` directory \
   The SDK version used at the time is `2023-04-18`.
2. Use a text editor and open `lib\foobar2000_sdk\foobar2000\helpers\foobar2000_sdk_helpers.vcxproj` \
   To accommodate the directory structure, do the followings:
   * Replace this line from:
     ```xml
     <Import Project="..\packages\wtl.10.0.10320\build\native\wtl.targets" Condition="Exists('..\packages\wtl.10.0.10320\build\native\wtl.targets')" />
     ```
     To:
     ```xml
     <Import Project="..\..\..\..\packages\wtl.10.0.10320\build\native\wtl.targets" Condition="Exists('..\..\..\..\packages\wtl.10.0.10320\build\native\wtl.targets')" />
     ```
   * Replace this line from:
     ```xml
     <Error Condition="!Exists('..\packages\wtl.10.0.10320\build\native\wtl.targets')" Text="$([System.String]::Format('$(ErrorText)', '..\packages\wtl.10.0.10320\build\native\wtl.targets'))" />
     ```
     To:
     ```xml
     <Error Condition="!Exists('..\..\..\..\packages\wtl.10.0.10320\build\native\wtl.targets')" Text="$([System.String]::Format('$(ErrorText)', '..\..\..\..\packages\wtl.10.0.10320\build\native\wtl.targets'))" />
     ```
3. Open `foo_test.sln`, select `OK` when asked whether to update compiler/libraries for old projects
4. Build `foo_test`!
5. `foo_test.dll` will be generated at `Debug`, `Release` or `x64` directory depending on your configuration
6. To load the component in foobar2000, copy the `foo_test.dll` into `foobar2000\components` directory

### Notes
* [Visual Studio Compatibility](https://wiki.hydrogenaud.io/index.php?title=Foobar2000:Development:Visual_Studio_Compatibility)

### Useful references
* [foobar2000 development tutorial](https://yirkha.fud.cz/tmp/496351ef.tutorial-draft.html)
* [Resources for foobar2000 component developers](https://foosion.foobar2000.org/developers/)
