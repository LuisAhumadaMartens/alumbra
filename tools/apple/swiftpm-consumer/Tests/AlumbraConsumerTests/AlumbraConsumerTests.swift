import Alumbra
import AlumbraConsumer
import XCTest

final class AlumbraConsumerTests: XCTestCase {
    @MainActor
    func testLifecycleLoop() throws {
        for _ in 0..<1000 {
            let player = try AlumbraPlayer()
            try player.load()
        }
    }

    @MainActor
    func testCallbacksAreDeliveredOnMainActor() async throws {
        let player = try AlumbraPlayer()
        var iterator = player.events.makeAsyncIterator()
        try player.load()
        let event = await iterator.next()
        XCTAssertNotNil(event)
        XCTAssertTrue(Thread.isMainThread)
    }
}
