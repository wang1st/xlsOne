import Foundation
import xlsOneCore

@main
struct XlsOneCLIApp {
    static func main() async {
        let cli = XlsOneCLI()
        await cli.run(args: CommandLine.arguments)
    }
}
