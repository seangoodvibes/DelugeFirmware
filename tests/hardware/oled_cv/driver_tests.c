#include "src/RZA1/oled/oled_low_level.c"

static void reset(void) {
	cvPendingChannels = 0;
	cvNextChannel = 0;
	spiBusCurrentlySending = false;
	cv_sending = oled_sending = false;
	oledFrameQueueReadPos = oledFrameQueueWritePos = 0;
	memset(oledFrameQueue, 0, sizeof(oledFrameQueue));
	oledWaitingForMessage = OLED_MESSAGE_NONE;
	oledPendingMessageToSend = 0;
	oledDmaWatchdogArmed = false;
	oledSettling = false;
	picRead = picWrite = picMessage = 0;
	picTxFullness = 0;
	memset(picRx, 0, sizeof(picRx));
	timer = notificationCount = criticalDepth = 0;
}

int main(void) {
	// Both channels survive a busy OLED bus; replacing CV1 cannot erase CV2.
	reset();
	spiBusCurrentlySending = true;
	enqueueCVMessage(0, 0x31111100);
	enqueueCVMessage(1, 0x32222200);
	enqueueCVMessage(0, 0x31333300);
	assert(notificationCount == 0);
	oledDeselectionComplete();
	assert(spi.SPDR.LONG == 0x31333300);
	assert(notificationCount == 0);
	cvSPITransferComplete(0);
	assert(spi.SPDR.LONG == 0x32222200);
	assert(notificationCount == 1 && notified[0] == 0);
	cvSPITransferComplete(0);
	assert(notificationCount == 2 && notified[1] == 1);
	assert(!spiBusCurrentlySending);

	// A new update on the channel in flight must not starve the other channel.
	reset();
	enqueueCVMessage(0, 0x31111100);
	enqueueCVMessage(0, 0x31222200);
	enqueueCVMessage(1, 0x32333300);
	cvSPITransferComplete(0);
	assert(spi.SPDR.LONG == 0x32333300);
	assert(notificationCount == 0); // CV1's first word is obsolete; do not release its gate.
	cvSPITransferComplete(0);
	assert(spi.SPDR.LONG == 0x31222200);
	assert(notificationCount == 1 && notified[0] == 1);
	cvSPITransferComplete(0);
	assert(notificationCount == 2 && notified[1] == 0);
	assert(!spiBusCurrentlySending && criticalDepth == 0);

	// CV may bypass a SELECT request only before it is sent to the PIC.
	reset();
	static uint8_t frame[768];
	// A grid redraw already queued on the PIC UART must not commit OLED SELECT.
	// CV can use the SPI bus while the unrelated pad bytes are still draining.
	oledFrameQueue[0] = frame;
	oledFrameQueueWritePos = 1;
	sendSPITransferFromQueue();
	picTxFullness = 392;
	oledRoutine();
	assert(picMessage == 0 && oledWaitingForMessage == OLED_MESSAGE_NONE);
	assert(oledPendingMessageToSend == OLED_MESSAGE_SELECT);
	enqueueCVMessage(0, 0x31123400);
	assert(spi.SPDR.LONG == 0x31123400);
	cvSPITransferComplete(0);
	oledRoutine();
	assert(picMessage == 0 && oledPendingMessageToSend == OLED_MESSAGE_SELECT);
	picTxFullness = 0;
	oledRoutine();
	assert(picMessage == OLED_MESSAGE_SELECT && oledWaitingForMessage == OLED_MESSAGE_SELECT);

	reset();
	oledFrameQueue[0] = frame;
	oledFrameQueueWritePos = 1;
	sendSPITransferFromQueue();
	enqueueCVMessage(0, 0x31123400);
	assert(picMessage == 0 && oledPendingMessageToSend == 0);
	assert(spi.SPDR.LONG == 0x31123400);
	assert(oledFrameQueueReadPos == 0); // The deferred frame is still queued.
	cvSPITransferComplete(0);
	picRx[0] = OLED_MESSAGE_SELECT; // A stale ack cannot satisfy a request not yet sent.
	picWrite = 1;
	oledRoutine();
	assert(picMessage == OLED_MESSAGE_SELECT);
	assert(picRx[0] == OLED_MESSAGE_ACK_CONSUMED && !oledSettling);
	enqueueCVMessage(1, 0x32567800);
	assert(spi.SPDR.LONG == 0x31123400); // Cannot interrupt a SELECT already sent.

	// Ack processing bypasses deferred UI input without disturbing release ordering.
	picRx[0] = 252; // NEXT_PAD_OFF
	picRx[1] = 42;  // Its corresponding pad event
	picRx[2] = OLED_MESSAGE_SELECT;
	picWrite = 3;
	oledRoutine();
	assert(picRead == 0 && picRx[0] == 252 && picRx[1] == 42);
	assert(picRx[2] == OLED_MESSAGE_ACK_CONSUMED);
	timer = msToSlowTimerCount(3) - 1;
	oledRoutine();
	assert(oledFrameQueueReadPos == 0);
	timer++;
	oledRoutine();
	assert(oledFrameQueueReadPos == 1 && dma.N0TB_n == sizeof(frame));
	oledTransferComplete(0);
	picTxFullness = 392; // Never postpone DESELECT: CV is already blocked until it completes.
	oledRoutine();
	assert(picMessage == OLED_MESSAGE_DESELECT); // No separate 10 ms dispatch wait.

	// Keep the settling requirement across timer wrap; duplicate acks must not extend it.
	timer = 65500;
	picRx[3] = OLED_MESSAGE_DESELECT;
	picWrite = 4;
	oledRoutine();
	timer = (uint16_t)(65500 + msToSlowTimerCount(3) - 1);
	picRx[4] = OLED_MESSAGE_DESELECT;
	picWrite = 5;
	oledRoutine();
	assert(spi.SPDR.LONG == 0x31123400);
	timer++;
	oledRoutine();
	assert(spi.SPDR.LONG == 0x32567800);
	assert(notificationCount == 1); // CV2 is still physically in flight.
	cvSPITransferComplete(0);
	assert(notificationCount == 2 && notified[1] == 1);
	assert(!spiBusCurrentlySending);

	// A wrapped receive window must leave already-consumed bytes alone.
	uint8_t ring[] = {248, 42, 248, 7, 8, 9, 252, 248};
	assert(uartReplaceByteInRing(ring, 8, 6, 2, 248, 255));
	assert(ring[7] == 255 && ring[0] == 255 && ring[2] == 248);
	assert(ring[6] == 252 && ring[1] == 42);
	assert(!uartReplaceByteInRing(ring, 8, 2, 2, 248, 255));
	puts("OLED/CV driver tests passed");
}
